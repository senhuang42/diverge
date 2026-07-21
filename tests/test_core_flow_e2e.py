from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from diverge.config import RunConfig
from diverge.embed import Embedder
from diverge.generator import MockGenerator
from diverge.session import run_session
from diverge.taste.events import TasteEventStore
from diverge.taste.model import TasteModel
from diverge.taste.profile import export_model, export_profile
from review.app import (
    comparison_candidates,
    load_bundle,
    record_choice,
    record_pairwise_choice,
    record_taste_choice,
    train_taste,
)

DATA = Path(__file__).parents[1] / "data"


class WeightFreeSpectralBackend:
    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray:
        del sample_rate
        output = np.zeros((len(audio), 512), dtype=np.float32)
        for row, signal in enumerate(audio):
            spectrum = np.abs(np.fft.rfft(signal))
            output[row] = [np.mean(chunk) for chunk in np.array_split(spectrum, 512)]
            output[row, row % 512] += 1e-4
        return output


def test_weight_free_core_flow_end_to_end(tmp_path: Path) -> None:
    """Permanent smoke alarm for generation, review, learning, and personalized reruns."""
    runs = tmp_path / "runs"
    choices = tmp_path / "choices.jsonl"
    taste_events = tmp_path / "taste" / "events.jsonl"
    taste_model = tmp_path / "taste" / "model.joblib"
    embedder = Embedder(
        model_id="weight-free-spectral-e2e",
        cache_dir=tmp_path / "cache",
        backend=WeightFreeSpectralBackend(),
    )
    first_config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[(DATA / "ref_a.wav", 1.0)],
        n_return=4,
        n_oversample=8,
        duration_s=0.25,
        seed=101,
        output_dir=runs,
        choices_path=choices,
        taste_events_path=taste_events,
        taste_model_path=taste_model,
        opinion=50,
    )

    first_run = run_session(first_config, MockGenerator(), embedder, progress=lambda _: None)
    first_bundle = load_bundle(first_run)
    assert len(first_bundle.candidates) == 4
    assert len(first_bundle.map_points) == 6
    assert not (first_run / ".oversample").exists()

    labels = ("keep", "keep", "discard", "discard")
    for candidate, label in zip(first_bundle.candidates, labels, strict=True):
        record_taste_choice(first_bundle, candidate, label, taste_events, embedder)
        record_choice(first_bundle, candidate, label, choices, embedder)
    pair = comparison_candidates(first_bundle.candidates, count=1)[0]
    comparison = record_pairwise_choice(
        first_bundle,
        pair[0],
        pair[1],
        "prefer_a",
        taste_events,
        embedder,
    )
    assert comparison is not None
    assert record_pairwise_choice(
        first_bundle,
        pair[1],
        pair[0],
        "prefer_b",
        taste_events,
        embedder,
    ) is None

    training = train_taste(taste_events, taste_model)
    assert training["observations"] == 5
    assert training["pairwise_observations"] == 1
    assert len(TasteEventStore(taste_events).load(effective=True)) == 5
    assert TasteModel.load(taste_model).observation_count == 5

    second_config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[(DATA / "ref_a.wav", 1.0)],
        n_return=4,
        n_oversample=8,
        duration_s=0.25,
        seed=202,
        output_dir=runs,
        choices_path=choices,
        taste_events_path=taste_events,
        taste_model_path=taste_model,
        opinion=90,
    )
    second_run = run_session(second_config, MockGenerator(), embedder, progress=lambda _: None)
    second_bundle = load_bundle(second_run)
    manifest = second_bundle.manifest

    assert second_run != first_run
    assert manifest["taste"]["observations"] == 5
    assert manifest["taste"]["event_watermark"] == comparison.event_id
    assert manifest["taste"]["confidence"] > 0
    assert manifest["taste"]["opinion"] == 90
    assert manifest["taste"]["warning"] is None
    assert len(second_bundle.candidates) == 4
    assert all(
        Path(candidate["path"]).stat().st_size > 1_000
        for candidate in second_bundle.candidates
    )
    assert any(candidate["effective_taste_weight"] > 0 for candidate in second_bundle.candidates)
    assert all("taste_uncertainty" in candidate for candidate in second_bundle.candidates)

    profile_json = export_profile(
        taste_model,
        taste_events,
        tmp_path / "taste" / "profile.json",
    )
    portable_model = export_model(
        taste_model,
        tmp_path / "taste" / "portable.joblib",
    )
    profile = json.loads(profile_json.read_text())
    assert profile["evidence"] == 5.0
    assert TasteModel.load(portable_model).event_watermark == comparison.event_id
