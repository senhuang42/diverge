import json
from pathlib import Path

import numpy as np

from diverge.audio_io import save_audio
from review.app import (
    comparison_candidates,
    load_bundle,
    record_choice,
    render_map_html,
    render_navigation_html,
    score_markdown,
)


class FakeEmbedder:
    def embed_file(self, path: Path) -> np.ndarray:
        vector = np.zeros(512, dtype=np.float32)
        vector[0] = 1
        return vector


def _bundle(tmp_path: Path):
    wav = save_audio(tmp_path / "cand_01.wav", np.ones((2, 2_000), dtype=np.float32) * 0.1)
    candidate = {
        "rank": 1,
        "path": str(wav),
        "ref_fit": 0.7,
        "locks": {"groove": 0.8},
        "novelty": 0.4,
        "taste": 0.5,
        "utility": 0.6,
    }
    (tmp_path / "manifest.json").write_text(json.dumps({"candidates": [candidate]}))
    points = [
        {"kind": "source", "path": "source.wav", "x": 0, "y": 0},
        {"kind": "reference", "path": "ref.wav", "x": 1, "y": 0},
        {"kind": "candidate", "path": str(wav), "rank": 1, "x": 0.5, "y": 1},
    ]
    (tmp_path / "map.json").write_text(json.dumps(points))
    return load_bundle(tmp_path), candidate


def test_review_bundle_map_and_scores(tmp_path: Path) -> None:
    bundle, candidate = _bundle(tmp_path)
    rendered = render_map_html(bundle.map_points)
    assert "Diverge embedding map" in rendered
    assert ">1</text>" in rendered
    assert 'href="#candidate-1"' in rendered
    assert 'data-candidate-rank="1"' in render_navigation_html(bundle.candidates)
    assert "Reference" in score_markdown(candidate)


def test_record_choice_appends_local_label(tmp_path: Path) -> None:
    bundle, candidate = _bundle(tmp_path)
    choices = tmp_path / "choices.jsonl"
    message = record_choice(bundle, candidate, "keep", choices, FakeEmbedder())
    row = json.loads(choices.read_text())
    assert row["label"] == "keep"
    assert row["path"].endswith("cand_01.wav")
    assert "Recorded" in message


def test_calibration_offers_six_distinct_targeted_pairs() -> None:
    candidates = [
        {"rank": index + 1, "taste": 0.4 + index * 0.02, "taste_uncertainty": 0.9}
        for index in range(5)
    ]
    pairs = comparison_candidates(candidates, 6)
    assert len(pairs) == 6
    assert len({frozenset((a["rank"], b["rank"])) for a, b in pairs}) == 6
