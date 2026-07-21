from __future__ import annotations

from itertools import combinations

import numpy as np

from .events import CandidateRecord, TasteEvent, TasteEventStore
from .features import CandidateContext
from .model import TasteModel
from .profile import training_events


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


def append_comparison(
    store: TasteEventStore,
    candidate_a: CandidateRecord,
    candidate_b: CandidateRecord,
    label: str,
    *,
    batch_id: str | None = None,
    strength: float = 1.0,
) -> TasteEvent | None:
    """Append one effective comparison, returning ``None`` for a repeated pair."""
    if label not in {"prefer_a", "prefer_b", "neither"}:
        raise ValueError("pairwise label must be prefer_a, prefer_b, or neither")
    if candidate_a.embedding_hash == candidate_b.embedding_hash:
        raise ValueError("cannot compare perceptually identical candidates")
    key = comparison_key(candidate_a.embedding_hash, candidate_b.embedding_hash)
    for event in training_events(store.path):
        if event.event_type != "pairwise" or not event.candidate_a or not event.candidate_b:
            continue
        previous = comparison_key(
            event.candidate_a.embedding_hash,
            event.candidate_b.embedding_hash,
        )
        if previous == key:
            return None
    return store.append(
        TasteEvent(
            event_type="pairwise",
            label=label,
            candidate_a=candidate_a,
            candidate_b=candidate_b,
            batch_id=batch_id,
            strength=strength,
        )
    )
