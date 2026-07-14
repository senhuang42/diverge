import json
from pathlib import Path

import numpy as np
import pytest

from diverge.select import Candidate, select_candidates
from diverge.taste.events import (
    CandidateRecord,
    TasteEvent,
    TasteEventStore,
    migrate_v1,
)
from diverge.taste.features import CandidateContext, FeatureTransform
from diverge.taste.feedback import choose_comparison, comparison_key
from diverge.taste.model import TasteModel


def vector(index: int) -> np.ndarray:
    value = np.zeros(512, dtype=np.float32)
    value[index] = 1
    return value


def event(index: int, label: str, path: str | None = None) -> TasteEvent:
    return TasteEvent(
        event_type="absolute",
        label=label,
        candidate_a=CandidateRecord.from_embedding(path or f"candidate-{index}.wav", vector(index)),
    )


def test_event_store_skips_malformed_and_undoes_without_rewriting(tmp_path: Path) -> None:
    path = tmp_path / "taste" / "events.jsonl"
    store = TasteEventStore(path)
    first = store.append(event(0, "keep", "same.wav"))
    store.append(event(1, "discard", "other.wav"))
    with path.open("a") as handle:
        handle.write("not-json\n")
    store.undo(first.event_id)
    with pytest.warns(RuntimeWarning):
        effective = store.load(effective=True)
    assert [item.label for item in effective] == ["discard"]
    assert len(path.read_text().splitlines()) == 4
    assert store.warnings


def test_later_absolute_supersedes_but_pairwise_is_distinct(tmp_path: Path) -> None:
    store = TasteEventStore(tmp_path / "events.jsonl")
    store.append(event(0, "keep", "same.wav"))
    store.append(event(0, "discard", "same.wav"))
    a = CandidateRecord.from_embedding("same.wav", vector(0))
    b = CandidateRecord.from_embedding("b.wav", vector(1))
    store.append(TasteEvent("pairwise", "prefer_a", a, b))
    store.append(TasteEvent("pairwise", "prefer_a", a, b))
    loaded = store.load(effective=True)
    assert [item.label for item in loaded].count("discard") == 1
    assert [item.label for item in loaded].count("prefer_a") == 2


def test_migration_is_stable_idempotent_and_preserves_embeddings(tmp_path: Path) -> None:
    choices = tmp_path / "choices.jsonl"
    row = {"path": "a.wav", "embedding": vector(2).tolist(), "label": "keep"}
    choices.write_text(json.dumps(row) + "\n")
    target = tmp_path / "taste" / "events.jsonl"
    assert migrate_v1(choices, target)["migrated"] == 1
    assert migrate_v1(choices, target)["migrated"] == 0
    loaded = TasteEventStore(target).load()
    assert np.array_equal(np.asarray(loaded[0].candidate_a.embedding), vector(2))


def test_features_are_deterministic_and_accept_empty_references() -> None:
    transform = FeatureTransform()
    context = CandidateContext(vector(4), source_embedding=vector(5), reference_embeddings=[])
    first = transform.transform(context)
    second = FeatureTransform().transform(context)
    assert np.array_equal(first.values, second.values)
    assert first.values[first.names.index("has_reference")] == 0
    ref_slice = first.groups["reference_difference"]
    assert np.count_nonzero(first.values[ref_slice]) == 0


def test_two_keeps_and_two_discards_shift_predictions_and_roundtrip(tmp_path: Path) -> None:
    events = [
        event(0, "keep"),
        event(0, "keep", "p2"),
        event(1, "discard"),
        event(1, "discard", "n2"),
    ]
    model = TasteModel()
    report = model.fit(events)
    prediction = model.score([CandidateContext(vector(0)), CandidateContext(vector(1))])
    assert report.observations == 4
    assert prediction.mean[0] > 0.5 > prediction.mean[1]
    path = model.save(tmp_path / "model.joblib")
    restored = TasteModel.load(path).score(
        [CandidateContext(vector(0)), CandidateContext(vector(1))]
    )
    assert np.allclose(prediction.mean, restored.mean)


def test_one_class_evidence_is_conservative() -> None:
    model = TasteModel()
    model.fit([event(0, "love"), event(0, "keep", "second")])
    prediction = model.score([CandidateContext(vector(0))])
    assert 0.5 < prediction.mean[0] < 0.8
    assert prediction.uncertainty[0] >= 0.45


def test_opinion_zero_removes_taste_influence() -> None:
    candidates = []
    for index, taste in enumerate((0.0, 1.0)):
        candidates.append(
            Candidate(
                index,
                vector(index),
                ref_fit=0.5,
                taste=taste,
                taste_uncertainty=0.0,
                taste_evidence=20,
            )
        )
    result = select_candidates(candidates, 1, spread=0, drift=0, opinion=0)
    assert result.selected[0].index == 0
    assert result.selected[0].effective_taste_weight == 0


def test_pairwise_ranker_and_active_pair_avoid_repeats() -> None:
    a = CandidateRecord.from_embedding("a.wav", vector(0))
    b = CandidateRecord.from_embedding("b.wav", vector(1))
    comparisons = [TasteEvent("pairwise", "prefer_a", a, b) for _ in range(6)]
    model = TasteModel()
    model.fit(comparisons)
    contexts = [CandidateContext(vector(0)), CandidateContext(vector(1))]
    prediction = model.score(contexts)
    assert prediction.mean[0] > prediction.mean[1]
    hashes = [a.embedding_hash, b.embedding_hash]
    assert choose_comparison(contexts, hashes, model) == (0, 1)
    asked = {comparison_key(*hashes)}
    assert choose_comparison(contexts, hashes, model, asked) is None
