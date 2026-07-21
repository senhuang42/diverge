from __future__ import annotations

from typing import Any

import numpy as np

from diverge.select import Candidate, select_candidates

from .evaluate import evaluation_models
from .events import TasteEvent
from .feedback import choose_comparison
from .model import TasteModel, _record_context
from .synthetic import PROFILE_NAMES, synthetic_events, synthetic_utility


def _auc(labels: np.ndarray, scores: np.ndarray) -> float:
    positive = np.flatnonzero(labels)
    negative = np.flatnonzero(~labels)
    if not len(positive) or not len(negative):
        return 0.5
    comparisons = [
        1.0 if scores[left] > scores[right] else 0.5 if scores[left] == scores[right] else 0.0
        for left in positive
        for right in negative
    ]
    return float(np.mean(comparisons))


def _utilities(profile: str, events: list[TasteEvent], offset: int = 0) -> np.ndarray:
    return np.asarray(
        [
            synthetic_utility(
                profile,
                np.asarray(event.candidate_a.embedding),
                str(event.candidate_a.features["source_category"]),
                offset + index,
            )
            for index, event in enumerate(events)
            if event.candidate_a
        ]
    )


def _few_shot(profile: str) -> dict[str, float]:
    events = synthetic_events(profile, decisions=80, seed=55)
    utilities = _utilities(profile, events)
    labels = np.asarray([event.label == "keep" for event in events])
    positive = np.flatnonzero(labels)
    negative = np.flatnonzero(~labels)
    chosen = [
        *positive[np.argsort(utilities[positive])[-2:]],
        *negative[np.argsort(utilities[negative])[:2]],
    ]
    chosen_set = {int(index) for index in chosen}
    model = TasteModel()
    model.fit([events[index] for index in chosen])
    held_out = [event for index, event in enumerate(events) if index not in chosen_set]
    scores = model.score(
        [_record_context(event.candidate_a, event) for event in held_out]
    ).mean
    held_out_labels = np.asarray([event.label == "keep" for event in held_out])
    auc = _auc(held_out_labels, scores)
    return {"auc": auc, "lift_over_neutral": auc - 0.5, "decisions": 4}


def _targeted_comparisons(profile: str) -> dict[str, float | int]:
    events = synthetic_events(profile, decisions=100, seed=42)
    utilities = _utilities(profile, events)
    comparisons: list[TasteEvent] = []
    used: set[int] = set()
    for category in ("drum", "melodic"):
        indexes = [
            index
            for index, event in enumerate(events)
            if event.candidate_a
            and event.candidate_a.features.get("source_category") == category
        ]
        indexes.sort(key=lambda index: utilities[index])
        for pair_index in range(3):
            low = indexes[pair_index]
            high = indexes[-1 - pair_index]
            comparisons.append(
                TasteEvent(
                    event_type="pairwise",
                    label="prefer_a",
                    candidate_a=events[high].candidate_a,
                    candidate_b=events[low].candidate_a,
                    batch_id="six-comparison-calibration",
                )
            )
            used.update((low, high))
    model = TasteModel()
    model.fit(comparisons)
    held_out = [event for index, event in enumerate(events) if index not in used]
    scores = model.score(
        [_record_context(event.candidate_a, event) for event in held_out]
    ).mean
    labels = np.asarray([event.label == "keep" for event in held_out])
    auc = _auc(labels, scores)
    return {"auc": auc, "lift_over_neutral": auc - 0.5, "comparisons": 6}


def _favorite_lift(profile: str) -> dict[str, float]:
    events = synthetic_events(profile, decisions=48, seed=12)
    train, pool = events[:24], events[24:]
    utilities = _utilities(profile, pool, offset=24)
    contexts = [_record_context(event.candidate_a, event) for event in pool]
    favorite_utility: dict[str, float] = {}
    for name in ("v1_logistic", "contextual"):
        model = evaluation_models()[name]
        model.fit(train)
        scores = model.score(contexts).mean
        favorites = np.argsort(-scores, kind="stable")[:3]
        favorite_utility[name] = float(np.mean(utilities[favorites]))
    return {
        **favorite_utility,
        "lift": favorite_utility["contextual"] - favorite_utility["v1_logistic"],
    }


def _uncertainty_reduction() -> dict[str, float]:
    events = synthetic_events("low_band", decisions=20, seed=8)
    train = events[:4]
    pool = events[4:12]
    contexts = [_record_context(event.candidate_a, event) for event in pool]
    hashes = [event.candidate_a.embedding_hash for event in pool]
    model = TasteModel()
    model.fit(train)
    before = float(np.mean(model.score(contexts).uncertainty))
    pair = choose_comparison(contexts, hashes, model)
    if pair is None:
        return {"before": before, "after": before, "reduction": 0.0}
    left, right = pair
    left_positive = pool[left].label == "keep"
    right_positive = pool[right].label == "keep"
    label = (
        "prefer_a"
        if left_positive and not right_positive
        else "prefer_b"
        if right_positive and not left_positive
        else "neither"
    )
    train.append(
        TasteEvent(
            event_type="pairwise",
            label=label,
            candidate_a=pool[left].candidate_a,
            candidate_b=pool[right].candidate_a,
        )
    )
    model.fit(train)
    after = float(np.mean(model.score(contexts).uncertainty))
    return {"before": before, "after": after, "reduction": before - after}


def _selection_safety() -> dict[str, float | int]:
    events = synthetic_events("two_mode", decisions=48, seed=12)
    train, pool = events[:24], events[24:]
    model = TasteModel()
    model.fit(train)
    contexts = [_record_context(event.candidate_a, event) for event in pool]
    prediction = model.score(contexts)
    utilities = _utilities("two_mode", pool, offset=24)

    def candidates() -> list[Candidate]:
        return [
            Candidate(
                index=index,
                embedding=np.asarray(event.candidate_a.embedding),
                ref_fit=0.5,
                taste=float(prediction.mean[index]),
                novelty=0.5,
                self_novelty=0.5,
                lock_score=1.0 if index < 12 else 0.1,
                taste_uncertainty=float(prediction.uncertainty[index]),
                taste_evidence=float(prediction.evidence[index]),
                taste_mode=prediction.mode_id[index],
            )
            for index, event in enumerate(pool)
        ]

    selected = select_candidates(candidates(), 8, spread=60, drift=20, opinion=100).selected
    neutral = select_candidates(candidates(), 8, spread=60, drift=20, opinion=0).selected
    distances = [
        1 - float(left.embedding @ right.embedding)
        for index, left in enumerate(selected)
        for right in selected[index + 1 :]
    ]
    high_favorites = [candidate.index for candidate in selected if candidate.role == "favorite"]
    neutral_favorites = [candidate.index for candidate in neutral if candidate.role == "favorite"]
    return {
        "lock_violations": sum(candidate.lock_score < 0.55 for candidate in selected),
        "mean_pairwise_distance": float(np.mean(distances)),
        "positive_modes_selected": len(
            {candidate.taste_mode for candidate in selected if candidate.taste_mode}
        ),
        "roles_selected": len({candidate.role for candidate in selected}),
        "high_opinion_favorite_utility": float(np.mean(utilities[high_favorites])),
        "zero_opinion_favorite_utility": float(np.mean(utilities[neutral_favorites])),
        "high_opinion_lift": float(
            np.mean(utilities[high_favorites]) - np.mean(utilities[neutral_favorites])
        ),
    }


def run_acceptance_simulations() -> dict[str, Any]:
    few_shot = {profile: _few_shot(profile) for profile in ("low_band", "dry_percussive")}
    comparisons = {profile: _targeted_comparisons(profile) for profile in PROFILE_NAMES}
    favorites = {profile: _favorite_lift(profile) for profile in PROFILE_NAMES}
    passing_comparison_profiles = sum(
        result["auc"] >= 0.65 for result in comparisons.values()
    )
    mean_favorite_lift = float(np.mean([result["lift"] for result in favorites.values()]))
    return {
        "few_shot": few_shot,
        "six_targeted_comparisons": comparisons,
        "comparison_profiles_at_or_above_0_65_auc": passing_comparison_profiles,
        "favorite_opinion_lift_vs_v1": favorites,
        "mean_favorite_opinion_lift_vs_v1": mean_favorite_lift,
        "informative_comparison": _uncertainty_reduction(),
        "selection": _selection_safety(),
    }
