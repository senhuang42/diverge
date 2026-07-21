from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from diverge.taste.evaluate import (
    MODEL_NAMES,
    chronological_folds,
    evaluate_events,
    evaluate_model,
)
from diverge.taste.events import CandidateRecord, TasteEvent
from diverge.taste.synthetic import synthetic_events


def test_chronological_folds_keep_batches_whole_and_ordered() -> None:
    events = synthetic_events("low_band", decisions=16)
    folds = chronological_folds(list(reversed(events)))

    assert len(folds) == 3
    for train, test in folds:
        train_batches = {event.batch_id for event in train}
        test_batches = {event.batch_id for event in test}
        assert train_batches.isdisjoint(test_batches)
        assert max(event.ts for event in train) < min(event.ts for event in test)
        assert len(test) == 4


def test_evaluation_scores_every_baseline_and_writes_reproducible_report(
    tmp_path: Path,
) -> None:
    events = synthetic_events("contextual_reversal", decisions=20, seed=7)
    report = evaluate_events(events, tmp_path)

    assert tuple(report["models"]) == MODEL_NAMES
    for metrics in report["models"].values():
        assert metrics["folds"] == 4
        assert metrics["absolute_predictions"] == 16
        assert metrics["ranked_batches"] == 4
        assert metrics["brier_score"] is not None
        assert 0 <= metrics["brier_score"] <= 1
        assert 0 <= metrics["ndcg"] <= 1
    assert report["models"]["neutral"]["brier_score"] == 0.25
    assert report["models"]["contextual"] != report["models"]["neutral"]
    assert set(report["synthetic_profiles"]) == {
        "low_band",
        "dry_percussive",
        "two_mode",
        "contextual_reversal",
        "noisy",
        "drift",
    }
    assert all(
        set(profile["models"]) == set(MODEL_NAMES)
        for profile in report["synthetic_profiles"].values()
    )
    assert all(
        metrics["brier_score"] is not None
        for metrics in report["synthetic_intervals"].values()
    )

    paths = list(tmp_path.glob("evaluation-*.json"))
    assert len(paths) == 1
    assert json.loads(paths[0].read_text()) == report


def test_pairwise_accuracy_is_scored_on_later_unseen_comparisons() -> None:
    left = np.zeros(512, dtype=np.float32)
    left[0] = 1
    right = np.zeros(512, dtype=np.float32)
    right[1] = 1
    a = CandidateRecord.from_embedding("a.wav", left)
    b = CandidateRecord.from_embedding("b.wav", right)
    events = [
        TasteEvent(
            event_type="pairwise",
            label="prefer_a",
            candidate_a=a,
            candidate_b=b,
            batch_id="train" if index < 4 else "test",
            ts=f"2026-01-01T{index:02d}:00:00+00:00",
        )
        for index in range(6)
    ]

    neutral = evaluate_model(events, "neutral")
    pairwise = evaluate_model(events, "prototype_pairwise")

    assert neutral["pairwise_predictions"] == 2
    assert neutral["pairwise_accuracy"] == 0.5
    assert pairwise["pairwise_accuracy"] == 1.0
