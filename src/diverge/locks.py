from __future__ import annotations

import numpy as np

from .features import groove_similarity, melody_similarity, timbre_similarity


def lock_similarities(
    candidate: np.ndarray,
    source: np.ndarray,
    candidate_embedding: np.ndarray,
    source_embedding: np.ndarray,
    sr: int,
) -> dict[str, float]:
    return {
        "groove": groove_similarity(candidate, source, sr),
        "melody": melody_similarity(candidate, source, sr),
        "timbre": timbre_similarity(candidate_embedding, source_embedding),
    }


def active_lock_score(scores: dict[str, float], locks: set[str]) -> float:
    return min((scores[name] for name in locks), default=1.0)
