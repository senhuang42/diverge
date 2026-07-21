import json
from pathlib import Path

import numpy as np

from diverge.benchmark import compare_benchmarks, load_corpus, run_benchmark
from diverge.embed import Embedder
from diverge.generator import MockGenerator

DATA = Path(__file__).parents[1] / "data"


class SpectralBackend:
    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray:
        output = np.zeros((len(audio), 512), dtype=np.float32)
        for row, signal in enumerate(audio):
            spectrum = np.abs(np.fft.rfft(signal))
            output[row, : min(512, len(spectrum))] = spectrum[:512]
            output[row, row] += 1e-4
        return output


def test_benchmark_records_evidence_and_corpus_gaps(tmp_path: Path) -> None:
    corpus_path = tmp_path / "corpus.json"
    corpus_path.write_text(
        json.dumps(
            {
                "version": 1,
                "cases": [
                    {
                        "id": "loop",
                        "source": str(DATA / "loop_a.wav"),
                        "source_class": "drums",
                        "prompt": "electronic drum loop",
                        "duration_s": 0.25,
                        "locks": [],
                    }
                ],
            }
        )
    )
    corpus = load_corpus(corpus_path)
    assert corpus.missing_classes
    embedder = Embedder(cache_dir=tmp_path / "cache", backend=SpectralBackend())
    report_path = run_benchmark(
        corpus,
        "mock",
        MockGenerator(),
        embedder,
        tmp_path / "reports",
        n_pool=3,
        n_return=2,
    )
    report = json.loads(report_path.read_text())
    assert report["corpus"]["representative"] is False
    assert report["summary"]["preserve_contract_passed"] is True
    assert report["summary"]["blind_quality_judgments"] == "pending"
    assert report["cases"][0]["selection"]["threshold_used"] == 0.55
    assert report["cases"][0]["selection"]["relaxations"] == []
    assert len(report["cases"][0]["candidates"]) == 3


def test_comparison_requires_matching_evidence_and_creates_blind_trials(tmp_path: Path) -> None:
    corpus_path = tmp_path / "corpus.json"
    corpus_path.write_text(
        json.dumps(
            {
                "version": 1,
                "cases": [
                    {
                        "id": "loop",
                        "source": str(DATA / "loop_a.wav"),
                        "source_class": "drums",
                        "prompt": "electronic drum loop",
                        "duration_s": 0.25,
                        "locks": [],
                    }
                ],
            }
        )
    )
    corpus = load_corpus(corpus_path)
    embedder = Embedder(cache_dir=tmp_path / "cache", backend=SpectralBackend())
    reports = [
        run_benchmark(
            corpus,
            engine,
            MockGenerator(),
            embedder,
            tmp_path / "reports",
            n_pool=2,
            n_return=1,
        )
        for engine in ("baseline", "candidate")
    ]
    comparison_path = compare_benchmarks(
        reports, "baseline", tmp_path / "comparison", blind_seed=7
    )
    comparison = json.loads(comparison_path.read_text())
    trials = json.loads((tmp_path / "comparison" / "blind_trials.json").read_text())
    assert comparison["questions"]["audio_to_audio_quality"]["status"] == (
        "pending_blind_judgments"
    )
    assert comparison["questions"]["preserve"]["no_silent_relaxation"] == {
        "baseline": True,
        "candidate": True,
    }
    assert comparison["decision"].startswith("pending_representative_corpus")
    assert len(trials) == 1
    assert Path(trials[0]["a"]).is_file()
    assert Path(trials[0]["b"]).is_file()
