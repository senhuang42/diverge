from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

DUPLICATE_SIMILARITY_THRESHOLD = 0.95


@dataclass
class Candidate:
    index: int
    embedding: np.ndarray
    ref_fit: float
    source_similarity: float = 1.0
    change_fit: float = 0.5
    taste: float = 0.5
    novelty: float = 0.5
    self_novelty: float = 0.5
    locks: dict[str, float] = field(default_factory=dict)
    lock_score: float = 1.0
    utility: float = 0.0
    taste_uncertainty: float | None = None
    taste_evidence: float | None = None
    taste_mode: str | None = None
    taste_factors: list[str] = field(default_factory=list)
    effective_taste_weight: float = 0.0
    role: str = "quality"


@dataclass
class SelectionResult:
    selected: list[Candidate]
    threshold_used: float
    relaxations: list[float]
    weights: dict[str, float]
    spread_lambda: float
    eligible_count: int
    requested_count: int
    duplicate_similarity_threshold: float


def spread_lambda(spread: int) -> float:
    normalized = np.clip(spread, 0, 100) / 100
    return round(1.5 * normalized**2, 6)


def change_alignment_score(source_similarity: float, transform: int) -> float:
    """Reward source identity at low Change and source distance at high Change."""
    change = float(np.clip(transform, 0, 100) / 100)
    similarity = float(np.clip(source_similarity, 0, 1))
    return (1 - change) * similarity + change * (1 - similarity)


def pairwise_similarity_metrics(candidates: list[Candidate]) -> dict[str, float]:
    if len(candidates) < 2:
        return {
            "mean_pairwise_similarity": 0.0,
            "max_pairwise_similarity": 0.0,
            "redundant_pair_fraction": 0.0,
        }
    embeddings = np.vstack([candidate.embedding for candidate in candidates])
    similarities = embeddings @ embeddings.T
    pairs = similarities[np.triu_indices(len(candidates), 1)]
    return {
        "mean_pairwise_similarity": round(float(np.mean(pairs)), 6),
        "max_pairwise_similarity": round(float(np.max(pairs)), 6),
        "redundant_pair_fraction": round(float(np.mean(pairs >= 0.9)), 6),
    }


def _utility(
    candidate: Candidate,
    drift: int,
    self_novelty_weight: float,
    opinion: int | None = None,
    configured_max_taste_weight: float = 0.60,
) -> tuple[float, dict[str, float]]:
    if opinion is None or candidate.taste_uncertainty is None or candidate.taste_evidence is None:
        candidate.effective_taste_weight = 0.3
    else:
        confidence = 1 - np.clip(candidate.taste_uncertainty, 0, 1)
        evidence_ramp = 1 - np.exp(-max(candidate.taste_evidence, 0) / 6)
        candidate.effective_taste_weight = round(
            float(confidence * evidence_ramp * configured_max_taste_weight * opinion / 100), 6
        )
    weights = {
        "ref_fit": 0.5,
        "change_fit": 0.4,
        "taste": candidate.effective_taste_weight,
        "novelty": round(0.2 * (1 + drift / 100) * (drift / 100), 6),
        "self_novelty": round(self_novelty_weight, 6),
    }
    value = sum(
        (
            candidate.ref_fit * weights["ref_fit"],
            candidate.change_fit * weights["change_fit"],
            (candidate.taste - 0.5) * weights["taste"],
            candidate.novelty * weights["novelty"],
            candidate.self_novelty * weights["self_novelty"],
        )
    )
    return round(float(value), 8), weights


def select_candidates(
    candidates: list[Candidate],
    n_return: int,
    spread: int,
    drift: int,
    lock_threshold: float = 0.55,
    self_novelty_weight: float = 0.05,
    opinion: int | None = None,
    configured_max_taste_weight: float = 0.60,
    allocate_roles: bool = True,
    duplicate_similarity_threshold: float = DUPLICATE_SIMILARITY_THRESHOLD,
) -> SelectionResult:
    if len(candidates) < n_return:
        raise ValueError("not enough candidates")
    for candidate in candidates:
        candidate.utility, weights = _utility(
            candidate,
            drift,
            self_novelty_weight,
            opinion,
            configured_max_taste_weight,
        )
    # Preserve is a hard contract. A sparse valid pool must produce a smaller set instead of
    # quietly weakening the threshold to fill every result slot.
    survivors = [c for c in candidates if c.lock_score >= lock_threshold]
    lam = spread_lambda(spread)
    remaining = sorted(survivors, key=lambda c: (-c.utility, c.index))
    if allocate_roles and opinion is not None and n_return == 8 and len(remaining) >= 12:
        chosen = _role_selection(remaining, lam, duplicate_similarity_threshold)
    elif not remaining:
        chosen = []
    elif lam == 0:
        chosen = []
        for candidate in remaining:
            if _not_duplicate(candidate, chosen, duplicate_similarity_threshold):
                chosen.append(candidate)
            if len(chosen) == n_return:
                break
    else:
        chosen = [remaining.pop(0)]
        while remaining and len(chosen) < n_return:
            remaining = [
                candidate
                for candidate in remaining
                if _not_duplicate(candidate, chosen, duplicate_similarity_threshold)
            ]
            if not remaining:
                break

            def objective(candidate: Candidate) -> tuple[float, int]:
                distances = [1 - float(candidate.embedding @ item.embedding) for item in chosen]
                return candidate.utility + lam * min(distances), -candidate.index

            winner = max(remaining, key=objective)
            remaining.remove(winner)
            chosen.append(winner)
    return SelectionResult(
        chosen,
        lock_threshold,
        [],
        weights,
        lam,
        eligible_count=len(survivors),
        requested_count=n_return,
        duplicate_similarity_threshold=duplicate_similarity_threshold,
    )


def _not_duplicate(
    candidate: Candidate,
    chosen: list[Candidate],
    threshold: float = DUPLICATE_SIMILARITY_THRESHOLD,
) -> bool:
    return all(float(candidate.embedding @ item.embedding) < threshold for item in chosen)


def _take_best(
    remaining: list[Candidate],
    chosen: list[Candidate],
    role: str,
    key,
    count: int,
    duplicate_similarity_threshold: float,
) -> None:
    for candidate in sorted(remaining, key=key, reverse=True):
        if len([item for item in chosen if item.role == role]) >= count:
            break
        if candidate in chosen or not _not_duplicate(
            candidate, chosen, duplicate_similarity_threshold
        ):
            continue
        candidate.role = role
        chosen.append(candidate)


def _role_selection(
    candidates: list[Candidate],
    lam: float,
    duplicate_similarity_threshold: float,
) -> list[Candidate]:
    """Allocate satisfaction, learning, exploration, and surprise capacity."""
    chosen: list[Candidate] = []
    _take_best(
        candidates,
        chosen,
        "favorite",
        lambda c: (c.utility, 1 - c.taste_uncertainty, -c.index),
        3,
        duplicate_similarity_threshold,
    )
    _take_best(
        candidates,
        chosen,
        "informative",
        lambda c: (
            (c.taste_uncertainty if c.taste_uncertainty is not None else 1.0)
            * (1 - min(1.0, 2 * abs(c.taste - 0.5))),
            c.utility,
            -c.index,
        ),
        2,
        duplicate_similarity_threshold,
    )

    def explore_score(candidate: Candidate) -> tuple[float, float, int]:
        distance = min(
            (1 - float(candidate.embedding @ item.embedding) for item in chosen), default=1.0
        )
        new_mode = float(
            bool(candidate.taste_mode)
            and candidate.taste_mode not in {item.taste_mode for item in chosen}
        )
        return new_mode + lam * distance, candidate.utility, -candidate.index

    _take_best(
        candidates,
        chosen,
        "explore",
        explore_score,
        2,
        duplicate_similarity_threshold,
    )
    _take_best(
        candidates,
        chosen,
        "surprise",
        lambda c: (0.6 * c.novelty + 0.4 * c.self_novelty, c.utility, -c.index),
        1,
        duplicate_similarity_threshold,
    )
    # Strict role allocation may be short; fill by utility without violating uniqueness.
    for candidate in candidates:
        if len(chosen) == 8:
            break
        if candidate not in chosen and _not_duplicate(
            candidate, chosen, duplicate_similarity_threshold
        ):
            candidate.role = "quality"
            chosen.append(candidate)
    return chosen
