from __future__ import annotations

import hashlib
import json
import os
import uuid
import warnings
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import numpy as np

SCHEMA_VERSION = 2
EVENT_TYPES = {"absolute", "pairwise", "profile_edit", "export", "undo"}
LABELS = {"love", "keep", "discard", "prefer_a", "prefer_b", "neither", "export", "undo"}


def embedding_hash(value: np.ndarray | list[float]) -> str:
    return hashlib.sha256(np.asarray(value, dtype=np.float32).tobytes()).hexdigest()


def _stable_id(row: dict[str, Any], index: int) -> str:
    payload = json.dumps(row, sort_keys=True, separators=(",", ":"), default=str)
    return str(uuid.uuid5(uuid.NAMESPACE_URL, f"diverge:v1:{index}:{payload}"))


@dataclass
class CandidateRecord:
    path: str
    embedding_hash: str
    embedding: list[float]
    features: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_embedding(
        cls, path: str | Path, embedding: np.ndarray, features: dict[str, Any] | None = None
    ) -> CandidateRecord:
        vector = np.asarray(embedding, dtype=np.float32).reshape(-1)
        if vector.size != 512:
            raise ValueError("CLAP embeddings must contain exactly 512 float32 values")
        return cls(str(path), embedding_hash(vector), vector.tolist(), features or {})

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> CandidateRecord:
        vector = np.asarray(value["embedding"], dtype=np.float32).reshape(-1)
        if vector.size != 512:
            raise ValueError("candidate embedding must have 512 values")
        return cls(
            path=str(value.get("path", "")),
            embedding_hash=str(value.get("embedding_hash") or embedding_hash(vector)),
            embedding=vector.tolist(),
            features=dict(value.get("features") or {}),
        )


@dataclass
class TasteEvent:
    event_type: str
    label: str
    candidate_a: CandidateRecord | None = None
    candidate_b: CandidateRecord | None = None
    strength: float = 1.0
    batch_id: str | None = None
    source_path: str | None = None
    source_embedding_hash: str | None = None
    reference_embedding_hashes: list[str] = field(default_factory=list)
    run_config: dict[str, Any] = field(default_factory=dict)
    event_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    ts: str = field(default_factory=lambda: datetime.now(UTC).isoformat())
    schema_version: int = SCHEMA_VERSION
    compensates_event_id: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.schema_version != SCHEMA_VERSION:
            raise ValueError(f"unsupported taste schema version {self.schema_version}")
        if self.event_type not in EVENT_TYPES:
            raise ValueError(f"unsupported event type: {self.event_type}")
        if self.label not in LABELS:
            raise ValueError(f"unsupported taste label: {self.label}")
        if self.event_type == "pairwise" and (self.candidate_a is None or self.candidate_b is None):
            raise ValueError("pairwise events require candidate_a and candidate_b")
        if self.event_type == "absolute" and self.candidate_a is None:
            raise ValueError("absolute events require candidate_a")
        self.strength = float(self.strength)
        if self.strength < 0:
            raise ValueError("strength must be non-negative")

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> TasteEvent:
        data = dict(value)
        data["candidate_a"] = (
            CandidateRecord.from_dict(data["candidate_a"]) if data.get("candidate_a") else None
        )
        data["candidate_b"] = (
            CandidateRecord.from_dict(data["candidate_b"]) if data.get("candidate_b") else None
        )
        return cls(**data)


@contextmanager
def _append_lock(handle: Any) -> Iterator[None]:
    if os.name == "posix":
        import fcntl

        fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
    else:  # pragma: no cover - Windows fallback still provides process atomic append
        yield


class TasteEventStore:
    def __init__(self, path: str | Path = "taste/events.jsonl") -> None:
        self.path = Path(path)
        self.warnings: list[str] = []

    def append(self, event: TasteEvent) -> TasteEvent:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(event.to_dict(), separators=(",", ":"), allow_nan=False) + "\n"
        with self.path.open("a", encoding="utf-8") as handle, _append_lock(handle):
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        return event

    def load(self, *, effective: bool = False) -> list[TasteEvent]:
        self.warnings.clear()
        if not self.path.exists():
            return []
        events: list[TasteEvent] = []
        with self.path.open(encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, 1):
                if not line.strip():
                    continue
                try:
                    events.append(TasteEvent.from_dict(json.loads(line)))
                except (ValueError, TypeError, KeyError, json.JSONDecodeError) as exc:
                    message = f"{self.path}:{line_number}: skipped malformed taste event: {exc}"
                    self.warnings.append(message)
                    warnings.warn(message, RuntimeWarning, stacklevel=2)
        return effective_events(events) if effective else events

    def undo(self, event_id: str) -> TasteEvent:
        known = {event.event_id for event in self.load()}
        if event_id not in known:
            raise KeyError(f"unknown taste event: {event_id}")
        return self.append(
            TasteEvent(
                event_type="undo",
                label="undo",
                strength=0.0,
                compensates_event_id=event_id,
            )
        )


def effective_events(events: list[TasteEvent]) -> list[TasteEvent]:
    """Apply undo and latest-absolute semantics without altering append history."""
    compensated = {
        event.compensates_event_id
        for event in events
        if event.event_type == "undo" and event.compensates_event_id
    }
    active = [
        event
        for event in events
        if event.event_id not in compensated and event.event_type != "undo"
    ]
    latest: dict[str, TasteEvent] = {}
    output: list[TasteEvent] = []
    for event in active:
        if event.event_type == "absolute" and event.candidate_a:
            key = event.candidate_a.path or event.candidate_a.embedding_hash
            latest[key] = event
        else:
            output.append(event)
    latest_ids = {event.event_id for event in latest.values()}
    return [
        event for event in active if event.event_id in latest_ids or event.event_type != "absolute"
    ]


def v1_events(path: str | Path = "choices.jsonl") -> tuple[list[TasteEvent], list[str]]:
    source = Path(path)
    if not source.exists():
        return [], []
    output: list[TasteEvent] = []
    problems: list[str] = []
    with source.open(encoding="utf-8") as handle:
        for index, line in enumerate(handle, 1):
            try:
                row = json.loads(line)
                if row.get("label") not in {"keep", "discard"} or "embedding" not in row:
                    raise ValueError("not a usable v1 decision")
                candidate = CandidateRecord.from_dict(row)
                output.append(
                    TasteEvent(
                        event_type="absolute",
                        label=row["label"],
                        candidate_a=candidate,
                        event_id=_stable_id(row, index),
                        ts=str(row.get("ts") or datetime.fromtimestamp(0, UTC).isoformat()),
                        metadata={"migrated_from": str(source), "v1_line": index},
                    )
                )
            except (ValueError, TypeError, KeyError, json.JSONDecodeError) as exc:
                problems.append(f"{source}:{index}: {exc}")
    return output, problems


def migrate_v1(
    choices_path: str | Path = "choices.jsonl", events_path: str | Path = "taste/events.jsonl"
) -> dict[str, Any]:
    events, problems = v1_events(choices_path)
    store = TasteEventStore(events_path)
    existing = {event.event_id for event in store.load()}
    added = 0
    for event in events:
        if event.event_id not in existing:
            store.append(event)
            added += 1
    return {"usable": len(events), "migrated": added, "skipped": problems, "path": str(store.path)}
