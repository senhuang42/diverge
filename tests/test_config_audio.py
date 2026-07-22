from pathlib import Path

import numpy as np
import pytest

from diverge.audio_io import load_audio, save_audio
from diverge.cli import _config, _parser
from diverge.config import RunConfig


def test_config_normalizes_references_and_roundtrips(tmp_path: Path) -> None:
    config = RunConfig(
        Path("source.wav"),
        [(Path("a.wav"), 0.2), (Path("b.wav"), 0.6)],
        host_context={"bpm": 124.0, "bpm_source": "host", "input_channels": 2},
    )
    assert [weight for _, weight in config.references] == pytest.approx([0.25, 0.75])
    assert RunConfig.from_json(config.to_json()).to_dict() == config.to_dict()


def test_config_rejects_invalid_generation_batch_size(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="generation_batch_size"):
        RunConfig(source=tmp_path / "source.wav", references=[], generation_batch_size=0)


def test_config_preserves_optional_branch_lineage(tmp_path: Path) -> None:
    source = tmp_path / "source.wav"
    config = RunConfig(
        source=source,
        references=[],
        parent_run_id="20260714T000400.676948Z",
        parent_candidate=5,
    )

    restored = RunConfig.from_json(config.to_json())

    assert restored.parent_run_id == "20260714T000400.676948Z"
    assert restored.parent_candidate == 5

    with pytest.raises(ValueError, match="parent_run_id"):
        RunConfig(source=source, references=[], parent_candidate=1)


def test_config_normalizes_numeric_strings_from_persisted_plugin_state(tmp_path: Path) -> None:
    config = RunConfig.from_dict(
        {
            "source": str(tmp_path / "source.wav"),
            "references": [],
            "reference_mix": "72",
            "transform": "80",
            "spread": "60",
            "drift": "0",
            "n_return": "8",
            "n_oversample": "16",
            "generation_batch_size": "8",
            "opinion": "50",
            "lock_threshold": "0.55",
            "parent_run_id": "20260714T000400.676948Z",
            "parent_candidate": "5",
        }
    )

    assert config.transform == 80
    assert config.reference_mix == 72
    assert config.parent_candidate == 5
    assert config.lock_threshold == 0.55


def test_fast_cli_uses_smaller_pool_unless_overridden() -> None:
    fast = _config(
        _parser().parse_args(
            ["run", "--source", "source.wav", "--reference-mix", "68", "--fast"]
        )
    )
    overridden = _config(
        _parser().parse_args(["run", "--source", "source.wav", "--fast", "--n-oversample", "24"])
    )
    assert fast.n_oversample == 16
    assert overridden.n_oversample == 24
    assert fast.duration_s is None
    assert fast.reference_mix == 68
    assert fast.locks == set()


def test_config_rejects_reference_mix_outside_source_reference_range() -> None:
    with pytest.raises(ValueError, match="reference_mix"):
        RunConfig(source=Path("source.wav"), references=[], reference_mix=101)


def test_run_cli_no_longer_exposes_preserve_locks() -> None:
    with pytest.raises(SystemExit):
        _parser().parse_args(["run", "--source", "source.wav", "--locks", "groove"])


def test_audio_preserves_mono_and_resamples_float(tmp_path: Path) -> None:
    path = save_audio(tmp_path / "audio.wav", np.ones(22_050, dtype=np.float32) * 0.2, 22_050)
    audio, sr = load_audio(path)
    assert sr == 44_100
    assert audio.shape == (1, 44_100)
    assert audio.dtype == np.float32
    assert np.max(np.abs(audio)) <= 0.98


def test_audio_preserves_stereo(tmp_path: Path) -> None:
    stereo = np.stack(
        [np.ones(1_000, dtype=np.float32) * 0.1, np.ones(1_000, dtype=np.float32) * 0.2]
    )
    path = save_audio(tmp_path / "stereo.wav", stereo)
    restored, _ = load_audio(path)
    assert restored.shape == (2, 1_000)
    assert restored[0].mean() == pytest.approx(0.1)
    assert restored[1].mean() == pytest.approx(0.2)
