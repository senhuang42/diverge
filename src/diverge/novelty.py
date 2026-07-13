from __future__ import annotations

import json
from pathlib import Path

import joblib
import numpy as np
from sklearn.neighbors import NearestNeighbors


def build_index(embeddings: np.ndarray, paths: list[str], output: str | Path) -> Path:
    embeddings = np.asarray(embeddings, dtype=np.float32)
    neighbors = NearestNeighbors(metric="cosine", n_neighbors=min(10, len(embeddings))).fit(
        embeddings
    )
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump({"embeddings": embeddings, "paths": paths, "neighbors": neighbors}, output)
    return output


def novelty_scores(
    embeddings: np.ndarray, index_path: str | Path | None, k: int = 10
) -> np.ndarray:
    if index_path is None:
        return np.full(len(embeddings), 0.5, dtype=np.float32)
    index = joblib.load(index_path)
    count = min(k, len(index["embeddings"]))
    distances, _ = index["neighbors"].kneighbors(embeddings, n_neighbors=count)
    return np.clip(distances.mean(axis=1), 0.0, 1.0).astype(np.float32)


def recent_kept_embeddings(
    choices_path: str | Path = "choices.jsonl", limit: int = 50
) -> np.ndarray | None:
    path = Path(choices_path)
    if not path.exists():
        return None
    latest = {}
    for line in path.read_text().splitlines():
        row = json.loads(line)
        if row.get("path") and row.get("label") in {"keep", "discard"}:
            latest[row["path"]] = row
    rows = [
        row["embedding"] for row in latest.values() if row["label"] == "keep" and "embedding" in row
    ]
    return np.asarray(rows[-limit:], dtype=np.float32) if rows else None


def self_novelty_scores(embeddings: np.ndarray, recent_keeps: np.ndarray | None) -> np.ndarray:
    if recent_keeps is None or len(recent_keeps) == 0:
        return np.full(len(embeddings), 0.5, dtype=np.float32)
    centroid = recent_keeps.mean(axis=0)
    centroid /= max(float(np.linalg.norm(centroid)), 1e-12)
    return np.clip(1 - embeddings @ centroid, 0.0, 1.0).astype(np.float32)
