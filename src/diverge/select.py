from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np


@dataclass
class Candidate:
    index: int
    embedding: np.ndarray
    ref_fit: float
    taste: float = 0.5
    novelty: float = 0.5
    self_novelty: float = 0.5
    locks: dict[str, float] = field(default_factory=dict)
    lock_score: float = 1.0
    utility: float = 0.0


@dataclass
class SelectionResult:
    selected: list[Candidate]
    threshold_used: float
    relaxations: list[float]
    weights: dict[str, float]
    spread_lambda: float


def spread_lambda(spread: int) -> float:
    return round(1.5 * np.clip(spread, 0, 100) / 100, 6)


def _utility(candidate: Candidate, drift: int) -> tuple[float, dict[str, float]]:
    weights = {
        "ref_fit": 0.5,
        "taste": 0.3,
        "novelty": round(0.2 * (1 + drift / 100) * (drift / 100), 6),
    }
    value = sum(
        (
            candidate.ref_fit * weights["ref_fit"],
            candidate.taste * weights["taste"],
            candidate.novelty * weights["novelty"],
        )
    )
    return round(float(value), 8), weights


def select_candidates(
    candidates: list[Candidate],
    n_return: int,
    spread: int,
    drift: int,
    lock_threshold: float = 0.55,
) -> SelectionResult:
    if len(candidates) < n_return:
        raise ValueError("not enough candidates")
    for candidate in candidates:
        candidate.utility, weights = _utility(candidate, drift)
    threshold = lock_threshold
    relaxations: list[float] = []
    survivors = [c for c in candidates if c.lock_score >= threshold]
    while len(survivors) < n_return and threshold > 0:
        threshold = round(max(0.0, threshold - 0.05), 2)
        relaxations.append(threshold)
        survivors = [c for c in candidates if c.lock_score >= threshold]
    lam = spread_lambda(spread)
    remaining = sorted(survivors, key=lambda c: (-c.utility, c.index))
    if lam == 0:
        chosen = remaining[:n_return]
    else:
        chosen = [remaining.pop(0)]
        while remaining and len(chosen) < n_return:

            def objective(candidate: Candidate) -> tuple[float, int]:
                distances = [1 - float(candidate.embedding @ item.embedding) for item in chosen]
                return candidate.utility + lam * min(distances), -candidate.index

            winner = max(remaining, key=objective)
            remaining.remove(winner)
            chosen.append(winner)
    return SelectionResult(chosen, threshold, relaxations, weights, lam)
