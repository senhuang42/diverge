from __future__ import annotations

from statistics import median
from typing import Any

_DELTA_LANGUAGE = {
    "dark": ("brighter texture", "darker texture"),
    "percussive": ("sparser rhythm", "busier rhythm"),
    "raw": ("cleaner texture", "rougher texture"),
    "compressed": ("more dynamic", "tighter dynamics"),
}


def candidate_explanations(
    candidates: list[dict[str, Any]],
    source_descriptors: dict[str, float],
    active_locks: set[str],
    lock_threshold: float,
    *,
    has_direction: bool,
    minimum_source_delta: float = 0.12,
    minimum_batch_delta: float = 0.06,
) -> list[dict[str, Any]]:
    """Explain only measured, batch-distinct reasons for including a candidate."""
    if not candidates:
        return []

    deltas = [
        {
            name: float(candidate.get("descriptors", {}).get(name, 0.0))
            - float(source_descriptors.get(name, 0.0))
            for name in _DELTA_LANGUAGE
        }
        for candidate in candidates
    ]
    direction_winner = _direction_winner(candidates) if has_direction else None
    explanations = []
    for index, candidate in enumerate(candidates):
        preserved = _strongest_preserved(candidate, active_locks, lock_threshold)
        if index == direction_winner:
            text = "Closest to your direction"
            if preserved:
                text += f"; {preserved} retained"
            explanations.append(
                {
                    "text": text + ".",
                    "evidence": {
                        "kind": "direction_fit",
                        "reference_fit": round(float(candidate.get("ref_fit", 0.0)), 6),
                        **({"preserved": preserved} if preserved else {}),
                    },
                }
            )
            continue

        best: tuple[float, str, float, float] | None = None
        for descriptor, source_delta in deltas[index].items():
            if abs(source_delta) < minimum_source_delta:
                continue
            other_values = [row[descriptor] for other, row in enumerate(deltas) if other != index]
            batch_delta = (
                abs(source_delta - median(other_values)) if other_values else abs(source_delta)
            )
            if other_values and batch_delta < minimum_batch_delta:
                continue
            score = abs(source_delta) + batch_delta
            if best is None or score > best[0]:
                best = (score, descriptor, source_delta, batch_delta)

        if best is None:
            explanations.append({"text": "", "evidence": {}})
            continue

        _, descriptor, source_delta, batch_delta = best
        phrase = _DELTA_LANGUAGE[descriptor][source_delta > 0]
        text = f"{preserved.title()} retained; {phrase}." if preserved else f"{phrase.title()}."
        explanations.append(
            {
                "text": text,
                "evidence": {
                    "kind": "source_delta",
                    "descriptor": descriptor,
                    "source_delta": round(source_delta, 6),
                    "batch_delta": round(batch_delta, 6),
                    **({"preserved": preserved} if preserved else {}),
                },
            }
        )
    return explanations


def _direction_winner(candidates: list[dict[str, Any]]) -> int | None:
    ordered = sorted(
        enumerate(candidates),
        key=lambda item: float(item[1].get("ref_fit", 0.0)),
        reverse=True,
    )
    winner_index, winner = ordered[0]
    winner_fit = float(winner.get("ref_fit", 0.0))
    runner_up_fit = float(ordered[1][1].get("ref_fit", 0.0)) if len(ordered) > 1 else 0.0
    return winner_index if winner_fit >= 0.65 and winner_fit - runner_up_fit >= 0.05 else None


def _strongest_preserved(
    candidate: dict[str, Any], active_locks: set[str], lock_threshold: float
) -> str | None:
    locks = candidate.get("locks", {})
    eligible = {
        name: float(locks.get(name, 0.0))
        for name in active_locks
        if float(locks.get(name, 0.0)) >= lock_threshold
    }
    return max(eligible, key=eligible.get) if eligible else None
