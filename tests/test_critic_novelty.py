from pathlib import Path

import numpy as np

from diverge.critic import (
    MIN_LABELS,
    add_choice,
    choice_count,
    load_choices,
    taste_scores,
    train_critic,
)
from diverge.novelty import build_index, novelty_scores, recent_kept_embeddings


def test_critic_stays_uninformative_then_trains(tmp_path: Path) -> None:
    choices = tmp_path / "choices.jsonl"
    model = tmp_path / "critic.joblib"
    rng = np.random.default_rng(0)
    for index in range(MIN_LABELS):
        vector = rng.normal(size=512).astype(np.float32)
        vector[0] = 4 if index % 2 else -4
        vector /= np.linalg.norm(vector)
        add_choice(f"{index}.wav", vector, "keep" if index % 2 else "discard", choices)
    result = train_critic(choices, model)
    assert result["trained"] is True
    scores = taste_scores(np.stack([np.eye(512)[0], -np.eye(512)[0]]), model)
    assert scores[0] > scores[1]


def test_critic_uses_latest_label_for_repeated_candidate(tmp_path: Path) -> None:
    choices = tmp_path / "choices.jsonl"
    vector = np.eye(512, dtype=np.float32)[0]
    add_choice("candidate.wav", vector, "keep", choices)
    add_choice("candidate.wav", vector, "discard", choices)
    x, y = load_choices(choices)
    assert x.shape == (1, 512)
    assert y.tolist() == [0]
    assert choice_count(choices) == 1


def test_recent_keeps_respects_latest_decision(tmp_path: Path) -> None:
    choices = tmp_path / "choices.jsonl"
    first = np.eye(512, dtype=np.float32)[0]
    second = np.eye(512, dtype=np.float32)[1]
    add_choice("changed.wav", first, "keep", choices)
    add_choice("changed.wav", first, "discard", choices)
    add_choice("kept.wav", second, "keep", choices)
    recent = recent_kept_embeddings(choices)
    assert recent is not None
    np.testing.assert_array_equal(recent, second[None, :])


def test_novelty_is_distance_from_library(tmp_path: Path) -> None:
    library = np.eye(3, dtype=np.float32)
    path = build_index(library, ["a", "b", "c"], tmp_path / "index.joblib")
    scores = novelty_scores(np.asarray([[1, 0, 0], [-1, 0, 0]], dtype=np.float32), path)
    assert scores[1] > scores[0]
