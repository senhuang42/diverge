from __future__ import annotations

import hashlib
import json
import platform
import random
import resource
import shutil
import subprocess
import sys
import time
from collections import Counter
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
    source_asset: str | None = None
    direction_asset: str | None = None


@dataclass(frozen=True)
class BenchmarkCorpus:
    path: Path
    cases: tuple[BenchmarkCase, ...]
    digest: str
    assets: dict[str, dict[str, Any]]
    minimum_cases_per_class: int = 1
    required_locks: frozenset[str] = frozenset({"groove", "melody", "timbre"})

    @property
    def represented_classes(self) -> set[str]:
        return {case.source_class for case in self.cases}

    @property
    def missing_classes(self) -> list[str]:
        return sorted(TARGET_SOURCE_CLASSES - self.represented_classes)

    @property
    def underrepresented_classes(self) -> list[str]:
        counts = Counter(case.source_class for case in self.cases)
        return sorted(
            source_class
            for source_class in TARGET_SOURCE_CLASSES
            if counts[source_class] < self.minimum_cases_per_class
        )

    @property
    def missing_locks(self) -> list[str]:
        represented = set().union(*(case.locks for case in self.cases))
        return sorted(self.required_locks - represented)

    @property
    def rights_metadata_complete(self) -> bool:
        required = {
            "path",
            "title",
            "creator",
            "landing_url",
            "license",
            "license_url",
            "license_reviewed_at",
            "source_sha256",
            "prepared_sha256",
        }
        used = {
            asset_id
            for case in self.cases
            for asset_id in (case.source_asset, case.direction_asset)
            if asset_id is not None
        }
        return bool(used) and all(required <= self.assets[asset_id].keys() for asset_id in used)

    @property
    def representative(self) -> bool:
        return (
            not self.underrepresented_classes
            and not self.missing_locks
            and self.rights_metadata_complete
        )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _audio_sha256(path: Path) -> str:
    audio, sample_rate = load_audio(path)
    digest = hashlib.sha256()
    digest.update(f"{sample_rate}:{audio.shape[0]}:{audio.shape[1]}".encode())
    digest.update(np.asarray(audio, dtype="<f4").tobytes(order="C"))
    return digest.hexdigest()


def load_corpus(path: str | Path) -> BenchmarkCorpus:
    corpus_path = Path(path).resolve()
    raw = corpus_path.read_bytes()
    payload = json.loads(raw)
    version = payload.get("version")
    if version not in (1, 2):
        raise ValueError("benchmark corpus version must be 1 or 2")
    assets = payload.get("assets", {})
    if version == 2 and not assets:
        raise ValueError("benchmark corpus version 2 requires assets")
    resolved_assets: dict[str, dict[str, Any]] = {}
    asset_hashes: dict[str, str] = {}
    for asset_id, asset in assets.items():
        resolved = dict(asset)
        asset_path = (corpus_path.parent / asset["path"]).resolve()
        if not asset_path.is_file():
            raise FileNotFoundError(
                f"{asset_path}; run scripts/prepare_evaluation_corpus.py first"
            )
        actual_hash = _audio_sha256(asset_path)
        expected_hash = asset.get("prepared_sha256")
        if expected_hash and actual_hash != expected_hash:
            raise ValueError(
                f"prepared checksum mismatch for {asset_id}: "
                f"expected {expected_hash}, got {actual_hash}"
            )
        resolved["path"] = str(asset_path)
        resolved["actual_sha256"] = actual_hash
        resolved_assets[str(asset_id)] = resolved
        asset_hashes[str(asset_id)] = actual_hash
    cases = []
    seen_ids: set[str] = set()
    for item in payload.get("cases", []):
        case_id = str(item["id"])
        if case_id in seen_ids:
            raise ValueError(f"duplicate benchmark case id: {case_id}")
        seen_ids.add(case_id)
        source_asset = item.get("source_asset")
        direction_asset = item.get("direction_asset")
        if source_asset is not None:
            if source_asset not in resolved_assets:
                raise ValueError(f"unknown source asset: {source_asset}")
            source = Path(resolved_assets[source_asset]["path"])
        else:
            source = (corpus_path.parent / item["source"]).resolve()
        if direction_asset is not None:
            if direction_asset not in resolved_assets:
                raise ValueError(f"unknown direction asset: {direction_asset}")
            direction_path = Path(resolved_assets[direction_asset]["path"])
        else:
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
                source_asset=source_asset,
                direction_asset=direction_asset,
            )
        )
    if not cases:
        raise ValueError("benchmark corpus must contain at least one case")
    digest = hashlib.sha256()
    digest.update(raw)
    digest.update(json.dumps(asset_hashes, sort_keys=True).encode())
    return BenchmarkCorpus(
        corpus_path,
        tuple(cases),
        digest.hexdigest(),
        resolved_assets,
        int(payload.get("minimum_cases_per_class", 1)),
        frozenset(payload.get("required_locks", ("groove", "melody", "timbre"))),
    )


def _system_profile() -> dict[str, Any]:
    def sysctl(name: str) -> str | None:
        try:
            return subprocess.run(
                ["sysctl", "-n", name],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip() or None
        except (FileNotFoundError, subprocess.CalledProcessError):
            return None

    memory = sysctl("hw.memsize")
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor() or sysctl("machdep.cpu.brand_string"),
        "model_identifier": sysctl("hw.model"),
        "memory_gb": round(int(memory) / 1024**3, 2) if memory else None,
        "python": platform.python_version(),
    }


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
    hardware_tier: str = "unclassified",
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
                "source_asset": case.source_asset,
                "source_class": case.source_class,
                "duration_s": case.duration_s,
                "locks": sorted(case.locks),
                "loop": case.loop,
                "direction_audio": str(case.direction_audio) if case.direction_audio else None,
                "direction_asset": case.direction_asset,
                "performance": {
                    "cold_start_included": case_index == 0,
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
    warm_first_times = [
        item["performance"]["first_playable_s"]
        for item in case_reports
        if not item["performance"]["cold_start_included"]
    ]
    warm_pool_times = [
        item["performance"]["full_pool_s"]
        for item in case_reports
        if not item["performance"]["cold_start_included"]
    ]
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
            "underrepresented_target_classes": corpus.underrepresented_classes,
            "minimum_cases_per_class": corpus.minimum_cases_per_class,
            "missing_required_locks": corpus.missing_locks,
            "rights_metadata_complete": corpus.rights_metadata_complete,
            "representative": corpus.representative,
            "assets": corpus.assets,
        },
        "run": {
            "n_pool": n_pool,
            "n_return": n_return,
            "lock_threshold": lock_threshold,
            "transform": transform,
            "seed": seed,
            "hardware_tier": hardware_tier,
        },
        "system": _system_profile(),
        "summary": {
            "p50_first_playable_s": float(np.median(first_times)),
            "p95_first_playable_s": float(np.percentile(first_times, 95)),
            "p50_full_pool_s": float(np.median(pool_times)),
            "p95_full_pool_s": float(np.percentile(pool_times, 95)),
            "cold_first_playable_s": first_times[0],
            "cold_full_pool_s": pool_times[0],
            "p50_warm_first_playable_s": (
                float(np.median(warm_first_times)) if warm_first_times else None
            ),
            "p95_warm_first_playable_s": (
                float(np.percentile(warm_first_times, 95)) if warm_first_times else None
            ),
            "p50_warm_full_pool_s": (
                float(np.median(warm_pool_times)) if warm_pool_times else None
            ),
            "p95_warm_full_pool_s": (
                float(np.percentile(warm_pool_times, 95)) if warm_pool_times else None
            ),
            "complete_valid_set_rate": float(np.mean(complete_sets)),
            "latency_budget_passed": bool(
                first_times[0] <= 20
                and pool_times[0] <= 60
                and (not warm_first_times or np.percentile(warm_first_times, 95) <= 20)
                and (not warm_pool_times or np.percentile(warm_pool_times, 95) <= 60)
            ),
            "minimum_hardware_latency_status": "pending",
            "preserve_contract_passed": all(
                not item["locks"]
                or (
                    item["selection"]["threshold_used"] == lock_threshold
                    and not item["selection"]["relaxations"]
                    and item["selection"]["returned_count"]
                    >= item["selection"]["requested_count"]
                    and all(
                        candidate["active_lock_score"] >= lock_threshold
                        for candidate in item["candidates"]
                        if candidate["selected"]
                    )
                )
                for item in case_reports
            ),
            "blind_quality_judgments": "pending",
        },
        "cases": case_reports,
    }
    if hardware_tier == "minimum":
        report["summary"]["minimum_hardware_latency_status"] = (
            "passed" if report["summary"]["latency_budget_passed"] else "failed"
        )
    else:
        report["summary"]["minimum_hardware_latency_status"] = (
            "not_measured_on_minimum_hardware"
        )
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
                        "judgments": [],
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
    baseline_cold_time = summaries[baseline_engine]["cold_full_pool_s"]
    baseline_warm_time = summaries[baseline_engine]["p50_warm_full_pool_s"]
    for _engine, summary in summaries.items():
        summary["speedup_vs_baseline"] = (
            baseline_time / summary["p50_full_pool_s"]
            if summary["p50_full_pool_s"] > 0
            else None
        )
        summary["cold_speedup_vs_baseline"] = (
            baseline_cold_time / summary["cold_full_pool_s"]
            if summary["cold_full_pool_s"] > 0
            else None
        )
        candidate_warm_time = summary["p50_warm_full_pool_s"]
        summary["warm_speedup_vs_baseline"] = (
            baseline_warm_time / candidate_warm_time
            if baseline_warm_time is not None
            and candidate_warm_time is not None
            and candidate_warm_time > 0
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
                    and summary["cold_speedup_vs_baseline"] is not None
                    and summary["cold_speedup_vs_baseline"] >= 4
                    and (
                        summary["warm_speedup_vs_baseline"] is None
                        or summary["warm_speedup_vs_baseline"] >= 4
                    )
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


def evaluate_benchmark_decision(
    comparison_dir: str | Path,
    *,
    legal_review_path: str | Path | None = None,
    minimum_judgments_per_engine: int = 20,
    quality_noninferiority_threshold: float = 0.50,
    minimum_useful_rate: float = 0.60,
) -> Path:
    root = Path(comparison_dir).resolve()
    comparison = json.loads((root / "comparison.json").read_text())
    trials = json.loads((root / "blind_trials.json").read_text())
    answer_key = {
        item["trial_id"]: item
        for item in json.loads((root / "blind_answer_key.json").read_text())
    }
    legal_review = (
        json.loads(Path(legal_review_path).read_text()) if legal_review_path is not None else {}
    )
    baseline = comparison["baseline_engine"]
    by_candidate: dict[str, list[tuple[str, str]]] = {}
    invalid_judgments: list[dict[str, str]] = []
    for trial in trials:
        entries = list(trial.get("judgments", []))
        if not entries and trial.get("winner") is not None:
            entries = [{"reviewer_id": "legacy", "winner": trial["winner"]}]
        seen_reviewers: set[str] = set()
        for entry in entries:
            reviewer_id = str(entry.get("reviewer_id", "")).strip()
            winner = str(entry.get("winner", "")).lower()
            invalid = not reviewer_id or winner not in {"a", "b", "tie", "neither"}
            if invalid or reviewer_id in seen_reviewers:
                invalid_judgments.append(
                    {
                        "trial_id": trial["trial_id"],
                        "reviewer_id": reviewer_id,
                        "winner": winner,
                    }
                )
                continue
            seen_reviewers.add(reviewer_id)
            if winner in {"a", "b"}:
                winner_engine = answer_key[trial["trial_id"]][f"{winner}_engine"]
            else:
                winner_engine = winner
            by_candidate.setdefault(trial["candidate_engine"], []).append(
                (winner_engine, reviewer_id)
            )

    quality: dict[str, dict[str, Any]] = {}
    legal: dict[str, dict[str, Any]] = {}
    candidates = sorted(
        engine for engine in comparison["engine_summaries"] if engine != baseline
    )
    legal_engines = legal_review.get("engines", {})
    for engine in candidates:
        judgments = by_candidate.get(engine, [])
        counts = Counter(winner for winner, _reviewer in judgments)
        useful_count = counts[engine] + counts[baseline] + counts["tie"]
        useful_rate = useful_count / len(judgments) if judgments else 0.0
        noninferiority_score = (
            (counts[engine] + 0.5 * counts["tie"]) / useful_count if useful_count else 0.0
        )
        quality[engine] = {
            "judgment_count": len(judgments),
            "candidate_wins": counts[engine],
            "baseline_wins": counts[baseline],
            "ties": counts["tie"],
            "neither_useful": counts["neither"],
            "useful_rate": useful_rate,
            "noninferiority_score": noninferiority_score,
            "passed": bool(
                len(judgments) >= minimum_judgments_per_engine
                and useful_rate >= minimum_useful_rate
                and noninferiority_score >= quality_noninferiority_threshold
            ),
        }
        review = legal_engines.get(engine, {})
        legal[engine] = {
            "status": review.get("status", "pending"),
            "reviewed_by": review.get("reviewed_by"),
            "reviewed_at": review.get("reviewed_at"),
            "scope": review.get("scope"),
            "passed": bool(
                review.get("status") == "approved"
                and review.get("reviewed_by")
                and review.get("reviewed_at")
                and review.get("scope")
            ),
        }

    decisions: dict[str, dict[str, Any]] = {}
    four_x = set(comparison["questions"]["latency"]["four_x_candidates"])
    preservation = comparison["questions"]["preserve"]["no_silent_relaxation"]
    complete_rates = comparison["questions"]["preserve"]["complete_valid_set_rate"]
    for engine in candidates:
        summary = comparison["engine_summaries"][engine]
        blockers = []
        if not comparison["corpus"]["representative"]:
            blockers.append("corpus_not_representative")
        if engine not in four_x:
            blockers.append("less_than_four_x_speedup")
        if summary["minimum_hardware_latency_status"] != "passed":
            blockers.append("minimum_hardware_latency_not_passed")
        if not quality[engine]["passed"]:
            blockers.append("blind_quality_not_passed")
        if not preservation[engine] or complete_rates[engine] < 1.0:
            blockers.append("preservation_not_passed")
        if not legal[engine]["passed"]:
            blockers.append("redistribution_not_approved")
        decisions[engine] = {"passed": not blockers, "blockers": blockers}

    passing = [engine for engine, result in decisions.items() if result["passed"]]
    decision = {
        "schema_version": 1,
        "baseline_engine": baseline,
        "thresholds": {
            "minimum_judgments_per_engine": minimum_judgments_per_engine,
            "quality_noninferiority_threshold": quality_noninferiority_threshold,
            "minimum_useful_rate": minimum_useful_rate,
            "required_complete_valid_set_rate": 1.0,
            "required_speedup": 4.0,
        },
        "invalid_judgments": invalid_judgments,
        "quality": quality,
        "legal": legal,
        "candidates": decisions,
        "decision": (
            {"status": "selected", "engine": passing[0]}
            if len(passing) == 1
            else {
                "status": "multiple_candidates_pass" if passing else "pending_or_rejected",
                "engines": passing,
            }
        ),
    }
    output = root / "decision.json"
    output.write_text(json.dumps(decision, indent=2))
    return output
