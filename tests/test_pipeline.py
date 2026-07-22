import json
from pathlib import Path

import numpy as np

from diverge.audio_io import load_audio, save_audio
from diverge.config import RunConfig
from diverge.embed import Embedder
from diverge.generator import MockGenerator
from diverge.session import run_session

DATA = Path(__file__).parents[1] / "data"


class SpectralBackend:
    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray:
        output = np.zeros((len(audio), 512), dtype=np.float32)
        for row, signal in enumerate(audio):
            spectrum = np.abs(np.fft.rfft(signal))
            chunks = np.array_split(spectrum, 512)
            output[row] = [np.mean(chunk) for chunk in chunks]
            output[row, row % 512] += 1e-4
        return output


def test_full_mock_session_writes_bundle(tmp_path: Path) -> None:
    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[(DATA / "ref_a.wav", 1)],
        n_return=3,
        n_oversample=6,
        duration_s=1,
        output_dir=tmp_path / "runs",
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )
    events: list[str] = []
    run_dir = run_session(config, MockGenerator(), embedder, progress=events.append)
    waves = sorted(run_dir.glob("cand_*.wav"))
    assert len(waves) == 3
    assert (run_dir / "manifest.json").exists()
    assert (run_dir / "map.json").exists()
    manifest = json.loads((run_dir / "manifest.json").read_text())
    assert len(manifest["candidates"]) == 3
    assert manifest["engine_capabilities"]["engine_id"] == "mock"
    assert manifest["selection"]["lock_threshold_requested"] == 0.55
    assert manifest["selection"]["lock_threshold_used"] == 0.55
    assert manifest["selection"]["relaxations"] == []
    assert manifest["selection"]["returned_count"] == 3
    assert manifest["selection"]["shortfall"] == 0
    assert "mean_source_similarity" in manifest["selection"]
    assert "max_pairwise_similarity" in manifest["selection"]
    assert "redundant_pair_fraction" in manifest["selection"]
    assert manifest["taste"]["version"] == 2
    assert "descriptors" in manifest["source_analysis"]
    assert all("explanation" in item for item in manifest["candidates"])
    assert all("explanation_evidence" in item for item in manifest["candidates"])
    assert all("source_similarity" in item for item in manifest["candidates"])
    assert all("change_fit" in item for item in manifest["candidates"])
    assert all("generation_prompt" in item for item in manifest["candidates"])
    structured = [
        json.loads(item.removeprefix("DIVERGE_EVENT "))
        for item in events
        if item.startswith("DIVERGE_EVENT ")
    ]
    assert [item["stage"] for item in structured if "completed" not in item] == [
        "preparing",
        "comparing",
        "choosing",
        "ready",
    ]
    assert structured[-2].get("stage") == "choosing"
    assert structured[-1] == {
        "stage": "ready",
        "requested_count": 3,
        "returned_count": 3,
        "shortfall": 0,
        "can_try_more": False,
    }
    assert all("taste_uncertainty" in item for item in manifest["candidates"])
    assert all(path.stat().st_size > 1_000 for path in waves)


def test_mock_session_without_references_uses_source_style(tmp_path: Path) -> None:
    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[],
        n_return=2,
        n_oversample=3,
        duration_s=0.25,
        output_dir=tmp_path / "runs",
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )
    run_dir = run_session(config, MockGenerator(), embedder, progress=lambda _: None)
    assert len(list(run_dir.glob("cand_*.wav"))) == 2


def test_explicit_duration_crops_the_source_region_before_generation(tmp_path: Path) -> None:
    seen: dict[str, int] = {}

    class SourceEchoGenerator(MockGenerator):
        def generate(
            self, source, sr, style_embedding, style_text_hint, transform, duration_s, seed, n
        ):
            del style_embedding, style_text_hint, transform, seed
            seen["samples"] = source.shape[-1]
            return [source.copy() for _ in range(n)]

    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[],
        n_return=1,
        n_oversample=1,
        duration_s=0.5,
        output_dir=tmp_path / "runs",
        locks=set(),
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )

    run_dir = run_session(config, SourceEchoGenerator(), embedder, progress=lambda _: None)
    manifest = json.loads((run_dir / "manifest.json").read_text())

    assert seen["samples"] == 22_050
    assert manifest["audio_contract"]["source_file_duration_s"] == 2.0
    assert manifest["audio_contract"]["source_duration_s"] == 0.5
    assert manifest["audio_contract"]["output_duration_s"] == 0.5
    assert manifest["audio_contract"]["source_fit"] == "cropped"


def test_mock_session_preserves_mono_output(tmp_path: Path) -> None:
    source = save_audio(
        tmp_path / "mono.wav",
        np.sin(np.linspace(0, 40 * np.pi, 11_025, dtype=np.float32)) * 0.2,
    )
    config = RunConfig(
        source=source,
        references=[],
        n_return=1,
        n_oversample=1,
        output_dir=tmp_path / "runs",
        locks=set(),
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )
    run_dir = run_session(config, MockGenerator(), embedder, progress=lambda _: None)
    result, _ = load_audio(run_dir / "cand_01.wav")
    manifest = json.loads((run_dir / "manifest.json").read_text())
    assert result.shape == (1, 11_025)
    assert manifest["audio_contract"]["source_channels"] == 1
    assert manifest["audio_contract"]["output_channels"] == 1


def test_source_duration_defaults_exactly_and_quality_failures_are_rejected(tmp_path: Path) -> None:
    class MixedQualityGenerator:
        emits_progress = False

        def generate(
            self, source, sr, style_embedding, style_text_hint, transform, duration_s, seed, n
        ):
            del style_embedding, style_text_hint, transform, seed
            samples = round(duration_s * sr)
            valid = source[:, :samples].copy()
            return [valid, np.zeros_like(valid), valid[:, :-1]][:n]

    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[],
        n_return=3,
        n_oversample=3,
        duration_s=None,
        output_dir=tmp_path / "runs",
        locks=set(),
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )
    run_dir = run_session(config, MixedQualityGenerator(), embedder, progress=lambda _: None)
    manifest = json.loads((run_dir / "manifest.json").read_text())
    assert manifest["audio_contract"]["source_fit"] == "exact"
    assert manifest["audio_contract"]["source_channels"] == 2
    assert manifest["audio_contract"]["output_channels"] == 2
    assert manifest["audio_contract"]["requested_duration_s"] is None
    assert manifest["selection"]["quality_rejected_count"] == 2
    assert manifest["selection"]["quality_failure_counts"] == {
        "silence": 1,
        "wrong_duration": 1,
    }
    assert manifest["selection"]["returned_count"] == 1
    assert manifest["selection"]["shortfall"] == 2
    assert manifest["selection"]["can_try_more"] is True
    assert len(manifest["candidates"]) == 1
    assert manifest["candidates"][0]["quality"]["passed"] is True


def test_all_rejected_candidates_write_a_recoverable_empty_run(tmp_path: Path) -> None:
    class SilentGenerator:
        emits_progress = False

        def generate(
            self, source, sr, style_embedding, style_text_hint, transform, duration_s, seed, n
        ):
            del source, style_embedding, style_text_hint, transform, seed
            return [np.zeros((2, round(duration_s * sr)), dtype=np.float32) for _ in range(n)]

    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[],
        n_return=2,
        n_oversample=2,
        output_dir=tmp_path / "runs",
        locks={"groove"},
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )

    run_dir = run_session(config, SilentGenerator(), embedder, progress=lambda _: None)
    manifest = json.loads((run_dir / "manifest.json").read_text())

    assert manifest["selection"]["returned_count"] == 0
    assert manifest["selection"]["shortfall"] == 2
    assert manifest["selection"]["can_try_more"] is True
    assert manifest["candidates"] == []
    assert json.loads((run_dir / "map.json").read_text()) == [
        {"kind": "source", "path": str(DATA / "loop_a.wav"), "x": 0.0, "y": 0.0}
    ]


def test_guaranteed_results_do_not_fill_slots_with_gain_duplicates(tmp_path: Path) -> None:
    class SilentGenerator:
        emits_progress = False

        def generate(
            self, source, sr, style_embedding, style_text_hint, transform, duration_s, seed, n
        ):
            del source, style_embedding, style_text_hint, transform, seed
            return [np.zeros((2, round(duration_s * sr)), dtype=np.float32) for _ in range(n)]

    config = RunConfig(
        source=DATA / "loop_a.wav",
        references=[],
        n_return=8,
        n_oversample=8,
        output_dir=tmp_path / "runs",
        locks={"groove", "melody", "timbre"},
        guarantee_results=True,
    )
    embedder = Embedder(
        model_id="spectral-test", cache_dir=tmp_path / "cache", backend=SpectralBackend()
    )

    run_dir = run_session(config, SilentGenerator(), embedder, progress=lambda _: None)
    manifest = json.loads((run_dir / "manifest.json").read_text())

    assert manifest["selection"]["returned_count"] == 1
    assert manifest["selection"]["shortfall"] == 7
    assert manifest["selection"]["fallback_selected_count"] == 1
    assert all(item["origin"] == "lock_safe_fallback" for item in manifest["candidates"])
    assert len(list(run_dir.glob("cand_*.wav"))) == 1
