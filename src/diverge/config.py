from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

LOCKS = {"groove", "melody", "timbre"}


@dataclass
class RunConfig:
    source: Path
    references: list[tuple[Path, float]]
    transform: int = 45
    spread: int = 60
    drift: int = 35
    locks: set[str] = field(default_factory=set)
    n_return: int = 8
    n_oversample: int = 32
    duration_s: float | None = None
    seed: int = 0
    library_index: Path | None = None
    critic_model: Path | None = None
    choices_path: Path = Path("choices.jsonl")
    style_text_hint: str = ""
    lock_threshold: float = 0.55
    fast: bool = False
    generation_batch_size: int = 8
    self_novelty_weight: float = 0.05
    output_dir: Path = Path("runs")
    taste_events_path: Path = Path("taste/events.jsonl")
    taste_model_path: Path = Path("taste/model.joblib")
    opinion: int = 50
    taste_learning_enabled: bool = True
    prompt_enrichment_enabled: bool = True
    guarantee_results: bool = False
    parent_run_id: str | None = None
    parent_candidate: int | None = None
    host_context: dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self.source = Path(self.source)
        self.references = [(Path(path), float(weight)) for path, weight in self.references]
        self.locks = set(self.locks)
        for name, value in (
            ("transform", self.transform),
            ("spread", self.spread),
            ("drift", self.drift),
        ):
            if not 0 <= value <= 100:
                raise ValueError(f"{name} must be in 0..100")
        if not self.locks <= LOCKS:
            raise ValueError(f"unknown locks: {sorted(self.locks - LOCKS)}")
        if self.n_return < 1 or self.n_oversample < self.n_return:
            raise ValueError("n_oversample must be >= n_return >= 1")
        if self.generation_batch_size < 1:
            raise ValueError("generation_batch_size must be >= 1")
        if self.duration_s is not None and not 0 < self.duration_s <= 30:
            raise ValueError("duration_s must be in (0, 30] when provided")
        if not 0 <= self.lock_threshold <= 1:
            raise ValueError("lock_threshold must be in 0..1")
        if not 0 <= self.self_novelty_weight <= 1:
            raise ValueError("self_novelty_weight must be in 0..1")
        if not 0 <= self.opinion <= 100:
            raise ValueError("opinion must be in 0..100")
        if self.parent_candidate is not None and not 1 <= self.parent_candidate <= self.n_return:
            raise ValueError("parent_candidate must identify a returned candidate")
        if self.parent_candidate is not None and not self.parent_run_id:
            raise ValueError("parent_run_id is required when parent_candidate is set")
        if self.references:
            if any(not 0 <= weight <= 1 for _, weight in self.references):
                raise ValueError("reference weights must be in 0..1")
            total = sum(weight for _, weight in self.references)
            if total <= 0:
                raise ValueError("at least one reference weight must be positive")
            normalized = [(path, round(weight / total, 12)) for path, weight in self.references]
            if len(normalized) > 1:
                normalized[-1] = (
                    normalized[-1][0],
                    round(1.0 - sum(weight for _, weight in normalized[:-1]), 12),
                )
            self.references = normalized
        self.library_index = Path(self.library_index) if self.library_index else None
        self.critic_model = Path(self.critic_model) if self.critic_model else None
        self.choices_path = Path(self.choices_path)
        self.output_dir = Path(self.output_dir)
        self.taste_events_path = Path(self.taste_events_path)
        self.taste_model_path = Path(self.taste_model_path)

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["source"] = str(self.source)
        result["references"] = [[str(p), w] for p, w in self.references]
        result["locks"] = sorted(self.locks)
        for key in (
            "library_index",
            "critic_model",
            "choices_path",
            "output_dir",
            "taste_events_path",
            "taste_model_path",
        ):
            result[key] = str(result[key]) if result[key] is not None else None
        return result

    def to_json(self, *, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> RunConfig:
        # Plugin state is persisted through JUCE XML before being written as JSON. JUCE may
        # restore numeric properties as strings, so normalize the external boundary before
        # dataclass validation performs numeric comparisons.
        normalized = dict(value)
        for key in (
            "transform",
            "spread",
            "drift",
            "n_return",
            "n_oversample",
            "seed",
            "generation_batch_size",
            "opinion",
            "parent_candidate",
        ):
            if normalized.get(key) is not None:
                normalized[key] = int(normalized[key])
        for key in ("duration_s", "lock_threshold", "self_novelty_weight"):
            if normalized.get(key) is not None:
                normalized[key] = float(normalized[key])
        return cls(**normalized)

    @classmethod
    def from_json(cls, value: str) -> RunConfig:
        return cls.from_dict(json.loads(value))
