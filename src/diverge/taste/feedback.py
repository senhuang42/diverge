from __future__ import annotations

from itertools import combinations

import numpy as np

from .features import CandidateContext
from .model import TasteModel


def comparison_key(a_hash: str, b_hash: str) -> tuple[str, str]:
    return tuple(sorted((a_hash, b_hash)))


def choose_comparison(
    contexts: list[CandidateContext],
    hashes: list[str],
    model: TasteModel,
    previously_asked: set[tuple[str, str]] | None = None,
    *,
    redundancy_cosine: float = 0.96,
) -> tuple[int, int] | None:
    """Choose an uncertain, non-redundant pair without repeating an effective comparison."""
    if len(contexts) != len(hashes):
        raise ValueError("contexts and hashes must have the same length")
    prediction = model.score(contexts)
    asked = previously_asked or set()
    best: tuple[float, int, int] | None = None
    for left, right in combinations(range(len(contexts)), 2):
        if comparison_key(hashes[left], hashes[right]) in asked:
            continue
        a = np.asarray(contexts[left].candidate_embedding, dtype=np.float32)
        b = np.asarray(contexts[right].candidate_embedding, dtype=np.float32)
        cosine = float(a @ b / max(float(np.linalg.norm(a) * np.linalg.norm(b)), 1e-12))
        if cosine >= redundancy_cosine:
            continue
        boundary = 1 - min(1.0, abs(float(prediction.mean[left] - prediction.mean[right])) * 2)
        uncertainty = float(prediction.uncertainty[left] + prediction.uncertainty[right]) / 2
        perceptual_difference = np.clip((1 - cosine) / 2, 0, 1)
        score = 0.45 * uncertainty + 0.35 * boundary + 0.20 * perceptual_difference
        candidate = (score, -left, -right)
        if best is None or candidate > best:
            best = candidate
    return (-best[1], -best[2]) if best else None
