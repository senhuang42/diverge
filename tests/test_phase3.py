from pathlib import Path

import numpy as np

from diverge.critic import add_choice, taste_scores, train_critic
from diverge.generator import MockGenerator
from diverge.select import Candidate, select_candidates


def test_trained_low_band_preference_shifts_mock_selection(tmp_path: Path) -> None:
    source = np.zeros((2, 4_410), dtype=np.float32)
    audio = MockGenerator().generate(
        source,
        44_100,
        np.zeros(512, dtype=np.float32),
        "",
        transform=100,
        duration_s=0.1,
        seed=17,
        n=40,
    )
    frequencies = np.fft.rfftfreq(audio[0].shape[-1], 1 / 44_100)
    centroids = np.asarray(
        [
            np.sum(frequencies * np.abs(np.fft.rfft(item.mean(axis=0))))
            / np.maximum(np.abs(np.fft.rfft(item.mean(axis=0))).sum(), 1e-12)
            for item in audio
        ]
    )
    scaled = np.clip(centroids / centroids.max(), 0, 1)
    embeddings = np.zeros((len(audio), 512), dtype=np.float32)
    embeddings[:, 0] = 1 - scaled
    embeddings[:, 1] = scaled
    embeddings /= np.linalg.norm(embeddings, axis=1, keepdims=True)

    choices = tmp_path / "choices.jsonl"
    cutoff = float(np.median(centroids))
    for index, embedding in enumerate(embeddings):
        add_choice(
            f"mock-{index}.wav",
            embedding,
            "keep" if centroids[index] < cutoff else "discard",
            choices,
        )
    model = tmp_path / "critic.joblib"
    assert train_critic(choices, model)["trained"] is True

    order = np.random.default_rng(4).permutation(len(audio))
    learned = taste_scores(embeddings[order], model)
    candidates = [
        Candidate(
            index=index,
            embedding=embeddings[source_index],
            ref_fit=0.5,
            taste=float(learned[index]),
            novelty=0.5,
            self_novelty=0.5,
        )
        for index, source_index in enumerate(order)
    ]
    selected = select_candidates(candidates, 8, spread=0, drift=0).selected
    selected_centroids = centroids[order[[item.index for item in selected]]]
    baseline_centroids = centroids[order[:8]]
    assert selected_centroids.mean() < baseline_centroids.mean() * 0.75
