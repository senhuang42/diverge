from __future__ import annotations

import hashlib
import json
from datetime import UTC, datetime
from pathlib import Path

import joblib
import numpy as np
from sklearn.linear_model import LogisticRegression

MIN_LABELS = 30


def embedding_hash(embedding: np.ndarray) -> str:
    return hashlib.sha256(np.asarray(embedding, dtype=np.float32).tobytes()).hexdigest()


def add_choice(
    path: str | Path,
    embedding: np.ndarray,
    label: str,
    choices_path: str | Path = "choices.jsonl",
) -> None:
    if label not in {"keep", "discard"}:
        raise ValueError("label must be keep or discard")
    row = {
        "path": str(path),
        "embedding_hash": embedding_hash(embedding),
        "embedding": np.asarray(embedding, dtype=np.float32).tolist(),
        "label": label,
        "ts": datetime.now(UTC).isoformat(),
    }
    with Path(choices_path).open("a") as handle:
        handle.write(json.dumps(row, separators=(",", ":")) + "\n")


def load_choices(path: str | Path = "choices.jsonl") -> tuple[np.ndarray, np.ndarray]:
    path = Path(path)
    if not path.exists():
        return np.empty((0, 512), dtype=np.float32), np.empty(0, dtype=np.int8)
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    usable = [row for row in rows if "embedding" in row and row.get("label") in {"keep", "discard"}]
    if not usable:
        return np.empty((0, 512), dtype=np.float32), np.empty(0, dtype=np.int8)
    latest = {}
    for row in usable:
        latest[row.get("path", row["embedding_hash"])] = row
    usable = list(latest.values())
    return (
        np.asarray([row["embedding"] for row in usable], dtype=np.float32),
        np.asarray([row["label"] == "keep" for row in usable], dtype=np.int8),
    )


def train_critic(
    choices_path: str | Path = "choices.jsonl", model_path: str | Path = "models/critic.joblib"
) -> dict[str, float | int | bool]:
    x, y = load_choices(choices_path)
    if len(y) < MIN_LABELS or len(np.unique(y)) < 2:
        return {"trained": False, "n": int(len(y)), "accuracy": 0.0}
    model = LogisticRegression(max_iter=1_000, random_state=0).fit(x, y)
    model_path = Path(model_path)
    model_path.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump(model, model_path)
    return {"trained": True, "n": int(len(y)), "accuracy": float(model.score(x, y))}


def taste_scores(embeddings: np.ndarray, model_path: str | Path | None) -> np.ndarray:
    if model_path is None or not Path(model_path).exists():
        return np.full(len(embeddings), 0.5, dtype=np.float32)
    model = joblib.load(model_path)
    return model.predict_proba(embeddings)[:, 1].astype(np.float32)
