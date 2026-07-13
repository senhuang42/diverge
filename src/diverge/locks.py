from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .features import (
    chroma,
    groove_similarity_envelopes,
    melody_similarity_chroma,
    onset_envelope,
    timbre_similarity,
)


@dataclass(frozen=True)
class LockSourceFeatures:
    onset: np.ndarray
    chroma: np.ndarray
    embedding: np.ndarray


def prepare_lock_source(
    source: np.ndarray, source_embedding: np.ndarray, sr: int
) -> LockSourceFeatures:
    return LockSourceFeatures(onset_envelope(source, sr), chroma(source, sr), source_embedding)


def lock_similarities(
    candidate: np.ndarray,
    source: np.ndarray,
    candidate_embedding: np.ndarray,
    source_embedding: np.ndarray,
    sr: int,
    source_features: LockSourceFeatures | None = None,
) -> dict[str, float]:
    prepared = source_features or prepare_lock_source(source, source_embedding, sr)
    return {
        "groove": groove_similarity_envelopes(onset_envelope(candidate, sr), prepared.onset, sr),
        "melody": melody_similarity_chroma(chroma(candidate, sr), prepared.chroma),
        "timbre": timbre_similarity(candidate_embedding, prepared.embedding),
    }


def active_lock_score(scores: dict[str, float], locks: set[str]) -> float:
    return min((scores[name] for name in locks), default=1.0)
