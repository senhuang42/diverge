from pathlib import Path

import numpy as np
import pytest

from diverge.audio_io import load_audio, save_audio
from diverge.config import RunConfig


def test_config_normalizes_references_and_roundtrips(tmp_path: Path) -> None:
    config = RunConfig(Path("source.wav"), [(Path("a.wav"), 0.2), (Path("b.wav"), 0.6)])
    assert [weight for _, weight in config.references] == pytest.approx([0.25, 0.75])
    assert RunConfig.from_json(config.to_json()).to_dict() == config.to_dict()


def test_audio_is_resampled_stereo_float(tmp_path: Path) -> None:
    path = save_audio(tmp_path / "audio.wav", np.ones(22_050, dtype=np.float32) * 0.2, 22_050)
    audio, sr = load_audio(path)
    assert sr == 44_100
    assert audio.shape == (2, 44_100)
    assert audio.dtype == np.float32
    assert np.max(np.abs(audio)) <= 0.98
