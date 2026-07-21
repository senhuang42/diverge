from __future__ import annotations

import hashlib
import json
import platform
import random
import resource
import shutil
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from .audio_io import load_audio, save_audio
from .embed import Embedder
from .generator import GeneratorProtocol
from .locks import active_lock_score, lock_similarities, prepare_lock_source
from .quality import evaluate_quality
from .select import Candidate, select_candidates

TARGET_SOURCE_CLASSES = {
    "drums",
    "melodic-loops",
    "bass",
    "recorded-instruments",
    "textures",
    "one-shots",
}


@dataclass(frozen=True)
class BenchmarkCase:
    case_id: str
    source: Path
    source_class: str
    prompt: str
    duration_s: float
    locks: frozenset[str]
    loop: bool = False
    direction_audio: Path | None = None


@dataclass(frozen=True)
class BenchmarkCorpus:
    path: Path
    cases: tuple[BenchmarkCase, ...]
    digest: str

    @property
    def represented_classes(self) -> set[str]:
        return {case.source_class for case in self.cases}

    @property
    def missing_classes(self) -> list[str]:
        return sorted(TARGET_SOURCE_CLASSES - self.represented_classes)


def load_corpus(path: str | Path) -> BenchmarkCorpus:
    corpus_path = Path(path).resolve()
    raw = corpus_path.read_bytes()
    payload = json.loads(raw)
    if payload.get("version") != 1:
        raise ValueError("benchmark corpus version must be 1")
    cases = []
    seen_ids: set[str] = set()
    for item in payload.get("cases", []):
        case_id = str(item["id"])
        if case_id in seen_ids:
            raise ValueError(f"duplicate benchmark case id: {case_id}")
        seen_ids.add(case_id)
        source = (corpus_path.parent / item["source"]).resolve()
        direction = item.get("direction_audio")
        direction_path = (corpus_path.parent / direction).resolve() if direction else None
        if not source.is_file():
            raise FileNotFoundError(source)
        if direction_path is not None and not direction_path.is_file():
            raise FileNotFoundError(direction_path)
        cases.append(
            BenchmarkCase(
                case_id=case_id,
                source=source,
                source_class=str(item["source_class"]),
                prompt=str(item.get("prompt", "")),
                duration_s=float(item["duration_s"]),
                locks=frozenset(item.get("locks", [])),
                loop=bool(item.get("loop", False)),
                direction_audio=direction_path,
            )
        )
    if not cases:
        raise ValueError("benchmark corpus must contain at least one case")
    return BenchmarkCorpus(corpus_path, tuple(cases), hashlib.sha256(raw).hexdigest())


def _peak_rss_mb() -> float:
    value = float(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    if sys.platform == "darwin":
        return value / (1024 * 1024)
    return value / 1024


def _progress_recorder(started: float, times: list[float]) -> Callable[[str], None]:
    def record(message: str) -> None:
        if message.startswith("PROGRESS "):
            times.append(time.perf_counter() - started)

    return record


def _redundant_pair_fraction(candidates: list[Candidate], threshold: float = 0.985) -> float:
    if len(candidates) < 2:
        return 0.0
    similarities = [
        float(left.embedding @ right.embedding)
        for index, left in enumerate(candidates)
        for right in candidates[index + 1 :]
    ]
    return sum(value >= threshold for value in similarities) / len(similarities)


def run_benchmark(
    corpus: BenchmarkCorpus,
    engine_id: str,
    generator: GeneratorProtocol,
    embedder: Embedder,
    output_dir: str | Path,
    *,
    n_pool: int = 16,
    n_return: int = 8,
    lock_threshold: float = 0.55,
    transform: int = 45,
    seed: int = 0,
) -> Path:
    if n_pool < n_return:
        raise ValueError("n_pool must be at least n_return")
    root = Path(output_dir).resolve() / engine_id
    root.mkdir(parents=True, exist_ok=True)
    case_reports: list[dict[str, Any]] = []
    for case_index, case in enumerate(corpus.cases):
        source, sr = load_audio(case.source)
        source_embedding = embedder.embed_file(case.source)
        direction_embedding = (
            embedder.embed_file(case.direction_audio)
            if case.direction_audio is not None
            else source_embedding
        )
        progress_times: list[float] = []
        started = time.perf_counter()

        if hasattr(generator, "progress"):
            generator.progress = _progress_recorder(started, progress_times)
        generated = generator.generate(
            source,
            sr,
            direction_embedding,
            case.prompt,
            transform,
            case.duration_s,
            seed + case_index * n_pool,
            n_pool,
        )
        generation_s = time.perf_counter() - started
        first_playable_s = progress_times[0] if progress_times else generation_s
        case_dir = root / case.case_id
        paths = [
            save_audio(case_dir / f"raw_{index + 1:03d}.wav", audio, sr)
            for index, audio in enumerate(generated)
        ]
        embeddings = embedder.embed_batch(paths)
        source_features = prepare_lock_source(source, source_embedding, sr)
        expected_samples = round(case.duration_s * sr)
        candidates: list[Candidate] = []
        candidate_reports = []
        for index, (audio, embedding, path) in enumerate(
            zip(generated, embeddings, paths, strict=True)
        ):
            quality = evaluate_quality(audio, expected_samples, loop=case.loop)
            similarities = lock_similarities(
                audio,
                source,
                embedding,
                source_embedding,
                sr,
                source_features=source_features,
            )
            lock_score = active_lock_score(similarities, set(case.locks))
            if not quality.passed:
                lock_score = -1.0
            source_identity = float(np.clip((embedding @ source_embedding + 1) / 2, 0, 1))
            direction_fit = float(
                np.clip((embedding @ direction_embedding + 1) / 2, 0, 1)
            )
            candidate = Candidate(
                index=index,
                embedding=embedding,
                ref_fit=direction_fit,
                locks=similarities,
                lock_score=lock_score,
            )
            candidates.append(candidate)
            candidate_reports.append(
                {
                    "index": index,
                    "path": str(path),
                    "source_identity": source_identity,
                    "direction_fit": direction_fit,
                    "locks": {name: float(value) for name, value in similarities.items()},
                    "active_lock_score": lock_score,
                    "quality": quality.to_dict(),
                }
            )
        selection = select_candidates(
            candidates,
            n_return,
            spread=60,
            drift=0,
            lock_threshold=lock_threshold,
            opinion=0,
            allocate_roles=False,
        )
        selected_indexes = {candidate.index for candidate in selection.selected}
        for candidate in candidate_reports:
            candidate["selected"] = candidate["index"] in selected_indexes
        case_reports.append(
            {
                "case_id": case.case_id,
                "source": str(case.source),
                "source_class": case.source_class,
                "duration_s": case.duration_s,
                "locks": sorted(case.locks),
                "loop": case.loop,
                "direction_audio": str(case.direction_audio) if case.direction_audio else None,
                "performance": {
                    "cold_start_included": True,
                    "first_playable_s": first_playable_s,
                    "full_pool_s": generation_s,
                    "peak_rss_mb": _peak_rss_mb(),
                },
                "selection": {
                    "threshold_requested": lock_threshold,
                    "threshold_used": selection.threshold_used,
                    "eligible_count": selection.eligible_count,
                    "returned_count": len(selection.selected),
                    "requested_count": n_return,
                    "relaxations": selection.relaxations,
                    "redundant_pair_fraction": _redundant_pair_fraction(selection.selected),
                },
                "candidates": candidate_reports,
            }
        )
    first_times = [item["performance"]["first_playable_s"] for item in case_reports]
    pool_times = [item["performance"]["full_pool_s"] for item in case_reports]
    complete_sets = [
        item["selection"]["returned_count"] >= item["selection"]["requested_count"]
        for item in case_reports
    ]
    capabilities = getattr(generator, "capabilities", None)
    report = {
        "schema_version": 1,
        "engine_id": engine_id,
        "engine_capabilities": capabilities.to_dict() if capabilities else None,
        "generator_settings": getattr(generator, "inference_settings", {}),
        "corpus": {
            "path": str(corpus.path),
            "sha256": corpus.digest,
            "case_count": len(corpus.cases),
            "represented_classes": sorted(corpus.represented_classes),
            "missing_target_classes": corpus.missing_classes,
            "representative": not corpus.missing_classes,
        },
        "run": {
            "n_pool": n_pool,
            "n_return": n_return,
            "lock_threshold": lock_threshold,
            "transform": transform,
            "seed": seed,
        },
        "system": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
        },
        "summary": {
            "p50_first_playable_s": float(np.median(first_times)),
            "p95_first_playable_s": float(np.percentile(first_times, 95)),
            "p50_full_pool_s": float(np.median(pool_times)),
            "p95_full_pool_s": float(np.percentile(pool_times, 95)),
            "complete_valid_set_rate": float(np.mean(complete_sets)),
            "latency_budget_passed": bool(
                np.percentile(first_times, 95) <= 20 and np.percentile(pool_times, 95) <= 60
            ),
            "preserve_contract_passed": all(
                item["selection"]["threshold_used"] == lock_threshold
                and not item["selection"]["relaxations"]
                for item in case_reports
            ),
            "blind_quality_judgments": "pending",
        },
        "cases": case_reports,
    }
    report_path = root / "benchmark.json"
    report_path.write_text(json.dumps(report, indent=2))
    return report_path


def _automated_summary(report: dict[str, Any]) -> dict[str, Any]:
    candidates = [candidate for case in report["cases"] for candidate in case["candidates"]]
    selected = [candidate for candidate in candidates if candidate["selected"]]
    quality_pass_rate = float(
        np.mean([candidate["quality"]["passed"] for candidate in candidates])
    )
    source_identity = (
        float(np.mean([candidate["source_identity"] for candidate in selected]))
        if selected
        else None
    )
    direction_fit = (
        float(np.mean([candidate["direction_fit"] for candidate in selected]))
        if selected
        else None
    )
    redundancy = float(
        np.mean([case["selection"]["redundant_pair_fraction"] for case in report["cases"]])
    )
    return {
        **report["summary"],
        "quality_pass_rate": quality_pass_rate,
        "mean_selected_source_identity": source_identity,
        "mean_selected_direction_fit": direction_fit,
        "mean_redundant_pair_fraction": redundancy,
    }


def compare_benchmarks(
    report_paths: list[str | Path],
    baseline_engine: str,
    output_dir: str | Path,
    *,
    blind_seed: int = 0,
) -> Path:
    if len(report_paths) < 2:
        raise ValueError("at least two benchmark reports are required")
    reports = [json.loads(Path(path).read_text()) for path in report_paths]
    by_engine = {report["engine_id"]: report for report in reports}
    if len(by_engine) != len(reports):
        raise ValueError("benchmark reports must have unique engine ids")
    if baseline_engine not in by_engine:
        raise ValueError(f"baseline engine not found: {baseline_engine}")
    corpus_digests = {report["corpus"]["sha256"] for report in reports}
    run_settings = {json.dumps(report["run"], sort_keys=True) for report in reports}
    if len(corpus_digests) != 1:
        raise ValueError("benchmark reports use different corpora")
    if len(run_settings) != 1:
        raise ValueError("benchmark reports use different run settings")

    root = Path(output_dir).resolve()
    audio_dir = root / "blind_audio"
    audio_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(blind_seed)
    trials = []
    answer_key = []
    baseline = by_engine[baseline_engine]
    baseline_cases = {case["case_id"]: case for case in baseline["cases"]}
    trial_index = 1
    for engine_id, report in sorted(by_engine.items()):
        if engine_id == baseline_engine:
            continue
        for case in report["cases"]:
            baseline_selected = [
                item for item in baseline_cases[case["case_id"]]["candidates"] if item["selected"]
            ]
            candidate_selected = [item for item in case["candidates"] if item["selected"]]
            for base_item, candidate_item in zip(
                baseline_selected, candidate_selected, strict=False
            ):
                sides = [
                    (baseline_engine, Path(base_item["path"])),
                    (engine_id, Path(candidate_item["path"])),
                ]
                rng.shuffle(sides)
                trial_id = f"trial_{trial_index:04d}"
                filenames = []
                for label, (_, source_path) in zip(("a", "b"), sides, strict=True):
                    target = audio_dir / f"{trial_id}_{label}.wav"
                    shutil.copy2(source_path, target)
                    filenames.append(str(target))
                trials.append(
                    {
                        "trial_id": trial_id,
                        "case_id": case["case_id"],
                        "candidate_engine": engine_id,
                        "a": filenames[0],
                        "b": filenames[1],
                        "winner": None,
                        "useful_audio_notes": "",
                    }
                )
                answer_key.append(
                    {
                        "trial_id": trial_id,
                        "a_engine": sides[0][0],
                        "b_engine": sides[1][0],
                    }
                )
                trial_index += 1
    summaries = {engine: _automated_summary(report) for engine, report in by_engine.items()}
    baseline_time = summaries[baseline_engine]["p50_full_pool_s"]
    for _engine, summary in summaries.items():
        summary["speedup_vs_baseline"] = (
            baseline_time / summary["p50_full_pool_s"]
            if summary["p50_full_pool_s"] > 0
            else None
        )
    comparison = {
        "schema_version": 1,
        "baseline_engine": baseline_engine,
        "corpus": baseline["corpus"],
        "run": baseline["run"],
        "engine_summaries": summaries,
        "questions": {
            "latency": {
                "status": "measured",
                "four_x_candidates": sorted(
                    engine
                    for engine, summary in summaries.items()
                    if engine != baseline_engine
                    and summary["speedup_vs_baseline"] is not None
                    and summary["speedup_vs_baseline"] >= 4
                ),
            },
            "audio_to_audio_quality": {
                "status": "pending_blind_judgments",
                "automated_metrics_are_not_a_preference_verdict": True,
            },
            "preserve": {
                "status": "measured",
                "no_silent_relaxation": {
                    engine: summary["preserve_contract_passed"]
                    for engine, summary in summaries.items()
                },
                "complete_valid_set_rate": {
                    engine: summary["complete_valid_set_rate"]
                    for engine, summary in summaries.items()
                },
            },
            "redistribution": {
                "status": "counsel_required",
                "engines": {
                    engine: report["engine_capabilities"]
                    for engine, report in by_engine.items()
                },
            },
        },
        "decision": (
            "pending_representative_corpus_blind_quality_and_legal_review"
            if not baseline["corpus"]["representative"]
            else "pending_blind_quality_and_legal_review"
        ),
        "blind_trials": str(root / "blind_trials.json"),
        "blind_answer_key": str(root / "blind_answer_key.json"),
    }
    (root / "blind_trials.json").write_text(json.dumps(trials, indent=2))
    (root / "blind_answer_key.json").write_text(json.dumps(answer_key, indent=2))
    comparison_path = root / "comparison.json"
    comparison_path.write_text(json.dumps(comparison, indent=2))
    return comparison_path
