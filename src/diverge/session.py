from __future__ import annotations

import json
from datetime import UTC, datetime
from pathlib import Path

import numpy as np

from .audio_io import load_audio, match_channels, save_audio
from .config import RunConfig
from .critic import taste_scores
from .embed import Embedder
from .explanations import candidate_explanations
from .fallback import lock_safe_variations
from .features import (
    chroma,
    groove_similarity_envelopes,
    melody_similarity_chroma,
    onset_envelope,
)
from .generator import GeneratorProtocol, transform_to_noise
from .locks import active_lock_score, lock_similarities, prepare_lock_source
from .map2d import project_2d
from .novelty import novelty_scores, recent_kept_embeddings, self_novelty_scores
from .quality import evaluate_quality
from .select import (
    Candidate,
    change_alignment_score,
    pairwise_similarity_metrics,
    select_candidates,
)
from .taste.events import TasteEventStore
from .taste.features import CandidateContext, audio_descriptors, descriptor_scores
from .taste.model import load_or_neutral
from .taste.profile import enrich_prompt, infer_descriptors


def _style_hint(config: RunConfig) -> str:
    if config.style_text_hint.strip():
        return config.style_text_hint.strip()
    return "coherent polished instrumental variation of the selected audio blend"


def _direction_descriptor_hint(
    config: RunConfig, reference_audio: list[np.ndarray]
) -> tuple[str, dict[str, float]]:
    if not config.references:
        return "", {"embedding": 1.0, "rhythm": 0.0, "melody": 0.0}
    weighted: dict[str, float] = {}
    for audio, (_, weight) in zip(reference_audio, config.references, strict=True):
        sr = 44_100
        spectral, temporal = audio_descriptors(audio, sr)
        for name, score in descriptor_scores(spectral, temporal).items():
            weighted[name] = weighted.get(name, 0.0) + weight * score
    onset_density = weighted.get("percussive", 0.0)
    if onset_density >= 0.30:
        rhythm = "dense chopped breakbeat rhythm with rapid syncopated attacks"
    elif onset_density >= 0.15:
        rhythm = "syncopated percussive rhythm with short articulated hits"
    else:
        rhythm = "sparse rhythm with long spaces between attacks"
    groups = (("dark", "bright"), ("raw", "polished"))
    descriptors = [rhythm]
    descriptors.extend(max(group, key=lambda name: weighted.get(name, 0.0)) for group in groups)
    if weighted.get("compressed", 0.0) >= 0.65:
        descriptors.append("compressed")
    component_weights = {"embedding": 0.40, "rhythm": 0.30, "melody": 0.30}
    return ", ".join(descriptors), component_weights


def run_session(
    config: RunConfig,
    generator: GeneratorProtocol,
    embedder: Embedder,
    *,
    progress=print,
) -> Path:
    def report(message: str) -> None:
        progress(message)
        if message.startswith("PROGRESS "):
            completed, total = message.removeprefix("PROGRESS ").split("/", maxsplit=1)
            progress(
                "DIVERGE_EVENT "
                + json.dumps(
                    {
                        "stage": "creating",
                        "completed": int(completed),
                        "total": int(total),
                    }
                )
            )

    def stage(name: str, **details: int | bool) -> None:
        progress(f"STAGE {name}")
        progress("DIVERGE_EVENT " + json.dumps({"stage": name, **details}))

    stage("preparing")
    source, sr = load_audio(config.source)
    source_file_duration_s = source.shape[-1] / sr
    if not 0.25 <= source_file_duration_s <= 30:
        raise ValueError("source audio must be between 0.25 and 30 seconds")
    requested_duration_s = config.duration_s
    duration_s = (
        min(requested_duration_s, source_file_duration_s)
        if requested_duration_s
        else source_file_duration_s
    )
    source = source[..., : round(duration_s * sr)].copy()
    reference_audio = [load_audio(path, sr)[0] for path, _ in config.references]
    if hasattr(generator, "capabilities"):
        minimum, maximum = generator.capabilities.duration_s
        if not minimum <= duration_s <= maximum:
            raise ValueError(
                f"{generator.capabilities.engine_id} supports source regions between "
                f"{minimum:.3f} and {maximum:.3f} seconds; received {duration_s:.3f} seconds"
            )
    source_spectral, source_temporal = audio_descriptors(source, sr)
    source_category = (
        "percussive"
        if source_temporal["onset_density"] >= 0.2
        else ("noisy" if source_spectral["flatness"] >= 0.25 else "melodic")
    )
    taste_model, taste_warning = load_or_neutral(config.taste_model_path)
    taste_events = TasteEventStore(config.taste_events_path).load(effective=True)
    hypotheses = infer_descriptors(taste_events)
    base_prompt = _style_hint(config)
    direction_hint, reference_component_weights = _direction_descriptor_hint(
        config, reference_audio
    )
    if float(config.host_context.get("bpm", 0) or 0) > 0:
        base_prompt += f", {round(float(config.host_context['bpm']))} BPM"
    prompt, prompt_additions = enrich_prompt(
        base_prompt,
        hypotheses,
        evidence=taste_model.effective_count,
        confidence=1 - np.exp(-taste_model.effective_count / 8),
        explicit_style_hint=bool(config.style_text_hint.strip()),
        enabled=config.prompt_enrichment_enabled,
    )
    reference_embeddings = embedder.embed_batch([path for path, _ in config.references])
    if len(reference_embeddings):
        weights = np.asarray([weight for _, weight in config.references], dtype=np.float32)
        style_embedding = weights @ reference_embeddings
        style_embedding /= max(float(np.linalg.norm(style_embedding)), 1e-12)
    else:
        style_embedding = embedder.embed_audio(source, sr)
    if hasattr(generator, "progress"):
        generator.progress = report
    if hasattr(generator, "preserve_locks"):
        generator.preserve_locks = set(config.locks)
    if hasattr(generator, "reference_directed"):
        generator.reference_directed = bool(config.references)
    if hasattr(generator, "reference_audio"):
        generator.reference_audio = reference_audio
        generator.reference_weights = [weight for _, weight in config.references]
        generator.reference_mix = config.reference_mix / 100
    generated = generator.generate(
        source,
        sr,
        style_embedding,
        prompt,
        config.transform,
        duration_s,
        config.seed,
        config.n_oversample,
    )
    source_channels = source.shape[0]
    generated = [match_channels(audio, source_channels) for audio in generated]
    generated_prompts = list(getattr(generator, "last_prompts", []))
    if len(generated_prompts) != len(generated):
        generated_prompts = [prompt] * len(generated)
    provenance = [
        {
            "kind": "model",
            "treatment": None,
            "source_equivalent_embedding": False,
            "prompt": generated_prompts[index],
        }
        for index in range(len(generated))
    ]
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    run_dir = config.output_dir / stamp
    staging = run_dir / ".oversample"
    staging.mkdir(parents=True, exist_ok=False)
    staging_paths = []
    for index, audio in enumerate(generated):
        if not getattr(generator, "emits_progress", False):
            report(f"PROGRESS {index + 1}/{config.n_oversample}")
        staging_paths.append(save_audio(staging / f"raw_{index:03d}.wav", audio, sr))
    stage("comparing")
    source_embedding = (
        style_embedding if not config.references else embedder.embed_audio(source, sr)
    )
    source_lock_features = (
        prepare_lock_source(source, source_embedding, sr)
        if config.locks or config.references
        else None
    )
    candidates: list[Candidate] = []
    contexts: list[CandidateContext] = []
    quality_reports = []
    reference_fit_components: list[dict[str, float]] = []
    reference_onsets = [onset_envelope(audio, sr) for audio in reference_audio]
    reference_chroma = [chroma(audio, sr) for audio in reference_audio]
    reference_weights = np.asarray(
        [weight for _, weight in config.references], dtype=np.float32
    )
    expected_samples = round(duration_s * sr)

    def analyze_batch(start: int, embeddings: np.ndarray) -> None:
        batch_audio = generated[start : start + len(embeddings)]
        batch_onsets = (
            [onset_envelope(audio, sr) for audio in batch_audio] if config.references else []
        )
        batch_chroma = [chroma(audio, sr) for audio in batch_audio] if config.references else []
        source_similarity = np.clip((embeddings @ source_embedding + 1) / 2, 0, 1)
        if config.references:
            embedding_fit = np.clip((embeddings @ style_embedding + 1) / 2, 0, 1)
            groove_fit = np.asarray(
                [
                    sum(
                        float(weight)
                        * groove_similarity_envelopes(
                            candidate_onset, reference_onset, sr
                        )
                        for weight, reference_onset in zip(
                            reference_weights, reference_onsets, strict=True
                        )
                    )
                    for candidate_onset in batch_onsets
                ],
                dtype=np.float32,
            )
            melody_fit = np.asarray(
                [
                    sum(
                        float(weight)
                        * melody_similarity_chroma(candidate_pitch, reference_pitch)
                        for weight, reference_pitch in zip(
                            reference_weights, reference_chroma, strict=True
                        )
                    )
                    for candidate_pitch in batch_chroma
                ],
                dtype=np.float32,
            )
        else:
            embedding_fit = np.full(len(embeddings), 0.5, dtype=np.float32)
            groove_fit = np.full(len(embeddings), 0.5, dtype=np.float32)
            melody_fit = np.full(len(embeddings), 0.5, dtype=np.float32)
        reference_fit_components.extend(
            {
                "embedding": round(float(embedding_value), 6),
                "rhythm": round(float(groove_value), 6),
                "melody": round(float(melody_value), 6),
            }
            for embedding_value, groove_value, melody_value in zip(
                embedding_fit, groove_fit, melody_fit, strict=True
            )
        )
        novelty = novelty_scores(embeddings, config.library_index)
        self_novelty = self_novelty_scores(
            embeddings, recent_kept_embeddings(config.choices_path)
        )
        legacy_taste = taste_scores(embeddings, config.critic_model)
        batch_contexts: list[CandidateContext] = []
        batch_similarities = []
        batch_blend_fit = []
        batch_change_fit = []
        batch_quality = []
        for offset, (audio, embedding, novelty_score, self_novelty_score) in enumerate(
            zip(batch_audio, embeddings, novelty, self_novelty, strict=True)
        ):
            quality = evaluate_quality(audio, expected_samples)
            similarities = (
                lock_similarities(
                    audio,
                    source,
                    embedding,
                    source_embedding,
                    sr,
                    source_features=source_lock_features,
                )
                if config.locks
                else {}
            )
            spectral, temporal = audio_descriptors(audio, sr)
            batch_quality.append(quality)
            batch_similarities.append(similarities)
            if config.references:
                mix = config.reference_mix / 100
                blend_fit = sum(
                    reference_component_weights[name]
                    * ((1 - mix) * source_value + mix * reference_value)
                    for name, source_value, reference_value in (
                        ("embedding", source_similarity[offset], embedding_fit[offset]),
                        (
                            "rhythm",
                            groove_similarity_envelopes(
                                batch_onsets[offset], source_lock_features.onset, sr
                            ),
                            groove_fit[offset],
                        ),
                        (
                            "melody",
                            melody_similarity_chroma(
                                batch_chroma[offset], source_lock_features.chroma
                            ),
                            melody_fit[offset],
                        ),
                    )
                )
            else:
                blend_fit = 0.5
            batch_blend_fit.append(float(blend_fit))
            batch_change_fit.append(change_alignment_score(float(blend_fit), config.transform))
            batch_contexts.append(
                CandidateContext(
                    candidate_embedding=embedding,
                    source_embedding=source_embedding,
                    reference_embeddings=reference_embeddings,
                    reference_weights=[weight for _, weight in config.references],
                    scores={
                        "groove": similarities.get("groove", 0.0),
                        "melody": similarities.get("melody", 0.0),
                        "timbre": similarities.get("timbre", 0.0),
                        "novelty": float(novelty_score),
                        "self_novelty": float(self_novelty_score),
                        "lock_score": active_lock_score(similarities, config.locks),
                    },
                    spectral=spectral,
                    temporal=temporal,
                    transform=config.transform,
                    spread=config.spread,
                    drift=config.drift,
                    locks=config.locks,
                    source_category=source_category,
                )
            )
        predictions = taste_model.score(batch_contexts)
        for offset, embedding in enumerate(embeddings):
            similarities = batch_similarities[offset]
            lock_score = active_lock_score(similarities, config.locks)
            if not batch_quality[offset].passed:
                lock_score = -1.0
            candidates.append(
                Candidate(
                    index=start + offset,
                    embedding=embedding,
                    ref_fit=batch_blend_fit[offset],
                    source_similarity=float(source_similarity[offset]),
                    change_fit=batch_change_fit[offset],
                    taste=(
                        float(predictions.mean[offset])
                        if taste_model.observation_count
                        else float(legacy_taste[offset])
                    ),
                    novelty=float(novelty[offset]),
                    self_novelty=float(self_novelty[offset]),
                    locks=similarities,
                    lock_score=lock_score,
                    taste_uncertainty=float(predictions.uncertainty[offset]),
                    taste_evidence=float(predictions.evidence[offset]),
                    taste_mode=predictions.mode_id[offset],
                    taste_factors=predictions.factors[offset],
                )
            )
        contexts.extend(batch_contexts)
        quality_reports.extend(batch_quality)

    model_embeddings = embedder.embed_batch(staging_paths)
    analyze_batch(0, model_embeddings)
    valid_model_count = sum(
        candidate.lock_score >= config.lock_threshold for candidate in candidates
    )
    missing = max(0, config.n_return - valid_model_count)
    if config.guarantee_results and missing:
        fallback = lock_safe_variations(
            source,
            sr,
            expected_samples,
            count=missing + min(4, missing),
            guaranteed_count=missing,
        )
        fallback_start = len(generated)
        generated.extend(match_channels(item.audio, source_channels) for item in fallback)
        provenance.extend(
            {
                "kind": "lock_safe_fallback",
                "treatment": item.treatment,
                "source_equivalent_embedding": item.source_equivalent_embedding,
                "prompt": None,
            }
            for item in fallback
        )
        fallback_paths = [
            save_audio(staging / f"raw_{fallback_start + index:03d}.wav", item.audio, sr)
            for index, item in enumerate(fallback)
        ]
        staging_paths.extend(fallback_paths)
        fallback_embeddings = np.empty((len(fallback), source_embedding.size), dtype=np.float32)
        measured_indexes = [
            index for index, item in enumerate(fallback) if not item.source_equivalent_embedding
        ]
        measured = embedder.embed_batch([fallback_paths[index] for index in measured_indexes])
        for index, embedding in zip(measured_indexes, measured, strict=True):
            fallback_embeddings[index] = embedding
        for index, item in enumerate(fallback):
            if item.source_equivalent_embedding:
                fallback_embeddings[index] = source_embedding
        analyze_batch(fallback_start, fallback_embeddings)
    stage("choosing")
    result = select_candidates(
        candidates,
        config.n_return,
        config.spread,
        config.drift,
        config.lock_threshold,
        config.self_novelty_weight,
        opinion=config.opinion,
    )
    records = []
    winner_embeddings = []
    for rank, candidate in enumerate(result.selected, start=1):
        path = save_audio(run_dir / f"cand_{rank:02d}.wav", generated[candidate.index], sr)
        winner_embeddings.append(candidate.embedding)
        records.append(
            {
                "rank": rank,
                "path": str(path),
                "oversample_index": candidate.index,
                "seed": (
                    config.seed + candidate.index
                    if provenance[candidate.index]["kind"] == "model"
                    else None
                ),
                "origin": provenance[candidate.index]["kind"],
                "treatment": provenance[candidate.index]["treatment"],
                "ref_fit": round(candidate.ref_fit, 6),
                "blend_fit": round(candidate.ref_fit, 6),
                "reference_embedding_fit": reference_fit_components[candidate.index][
                    "embedding"
                ],
                "reference_rhythm_fit": reference_fit_components[candidate.index]["rhythm"],
                "reference_melody_fit": reference_fit_components[candidate.index]["melody"],
                "source_similarity": round(candidate.source_similarity, 6),
                "change_fit": round(candidate.change_fit, 6),
                "generation_prompt": provenance[candidate.index]["prompt"],
                "locks": {k: round(v, 6) for k, v in candidate.locks.items()},
                "lock_score": round(candidate.lock_score, 6),
                "novelty": round(candidate.novelty, 6),
                "self_novelty": round(candidate.self_novelty, 6),
                "taste": round(candidate.taste, 6),
                "taste_uncertainty": round(candidate.taste_uncertainty, 6),
                "taste_evidence": round(candidate.taste_evidence, 6),
                "taste_mode": candidate.taste_mode,
                "taste_factors": candidate.taste_factors,
                "descriptors": descriptor_scores(
                    contexts[candidate.index].spectral, contexts[candidate.index].temporal
                ),
                "source_category": source_category,
                "effective_taste_weight": candidate.effective_taste_weight,
                "role": candidate.role,
                "utility": candidate.utility,
                "quality": quality_reports[candidate.index].to_dict(),
            }
        )
    source_descriptors = descriptor_scores(source_spectral, source_temporal)
    explanations = candidate_explanations(
        records,
        source_descriptors,
        config.locks,
        config.lock_threshold,
        # Blend fit symmetrically combines source/reference embedding, groove, and melody
        # similarity. Text direction is model input without a separate calibrated fit score.
        has_direction=bool(config.references),
    )
    for record, explanation in zip(records, explanations, strict=True):
        record["explanation"] = (
            f"Lock-safe source treatment: {record['treatment']}."
            if record["origin"] == "lock_safe_fallback"
            else explanation["text"]
        )
        record["explanation_evidence"] = explanation["evidence"]
    diversity_metrics = pairwise_similarity_metrics(result.selected)
    manifest = {
        "config": config.to_dict(),
        "model_ids": {"embedder": embedder.model_id, "generator": type(generator).__name__},
        "engine_capabilities": (
            generator.capabilities.to_dict() if hasattr(generator, "capabilities") else None
        ),
        "generator_settings": getattr(
            generator,
            "last_inference_settings",
            getattr(generator, "inference_settings", {}),
        ),
        "audio_contract": {
            "source_duration_s": duration_s,
            "source_file_duration_s": source_file_duration_s,
            "requested_duration_s": requested_duration_s,
            "output_duration_s": duration_s,
            "source_fit": (
                "exact"
                if np.isclose(duration_s, source_file_duration_s)
                else "cropped"
            ),
            "sample_rate": sr,
            "source_channels": source_channels,
            "output_channels": source_channels,
            "expected_samples": expected_samples,
        },
        "source_analysis": {"descriptors": source_descriptors},
        "reference_influence": {
            "native_model_audio_direction": bool(
                getattr(getattr(generator, "capabilities", None), "audio_direction", False)
            ),
            "derived_text_direction": False,
            "explicit_text_direction": bool(config.style_text_hint.strip()),
            "symmetric_morph_prompting": bool(config.references),
            "reference_mix": config.reference_mix,
            "full_audio_conditioning": bool(
                config.references and hasattr(generator, "reference_conditioning_mode")
            ),
            "conditioning_mode": getattr(
                generator, "reference_conditioning_mode", "unsupported"
            ),
            "selection_component_weights": reference_component_weights,
            "descriptor_hint": direction_hint,
        },
        "calibration": {
            "transform_noise": {
                "min": 0.1,
                "max": 1.0,
                "used": transform_to_noise(config.transform),
            },
            "spread_lambda": result.spread_lambda,
        },
        "selection": {
            "lock_threshold_requested": config.lock_threshold,
            "lock_threshold_used": result.threshold_used,
            "relaxations": result.relaxations,
            "eligible_count": result.eligible_count,
            "duplicate_similarity_threshold": result.duplicate_similarity_threshold,
            "requested_count": result.requested_count,
            "returned_count": len(result.selected),
            "shortfall": max(0, result.requested_count - len(result.selected)),
            "can_try_more": len(result.selected) < result.requested_count,
            "quality_rejected_count": sum(not report.passed for report in quality_reports),
            "quality_failure_counts": {
                reason: sum(reason in report.failures for report in quality_reports)
                for reason in sorted(
                    {reason for report in quality_reports for reason in report.failures}
                )
            },
            "utility_weights": result.weights,
            "model_pool_count": config.n_oversample,
            "fallback_pool_count": sum(
                item["kind"] == "lock_safe_fallback" for item in provenance
            ),
            "fallback_selected_count": sum(
                provenance[candidate.index]["kind"] == "lock_safe_fallback"
                for candidate in result.selected
            ),
            "mean_source_similarity": round(
                float(
                    np.mean([candidate.source_similarity for candidate in result.selected])
                    if result.selected
                    else 0.0
                ),
                6,
            ),
            **diversity_metrics,
        },
        "taste": {
            "version": 2,
            "event_watermark": taste_model.event_watermark,
            "observations": taste_model.observation_count,
            "confidence": round(1 - np.exp(-taste_model.effective_count / 8), 6),
            "opinion": config.opinion,
            "warning": taste_warning,
            "prompt_base": base_prompt,
            "prompt_used": prompt,
            "prompt_additions": prompt_additions,
        },
        "candidates": records,
    }
    (run_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    all_embeddings = np.vstack([source_embedding, *reference_embeddings, *winner_embeddings])
    coords = project_2d(all_embeddings)
    labels = [{"kind": "source", "path": str(config.source)}]
    labels += [{"kind": "reference", "path": str(path)} for path, _ in config.references]
    labels += [{"kind": "candidate", "path": row["path"], "rank": row["rank"]} for row in records]
    (run_dir / "map.json").write_text(
        json.dumps(
            [
                {**label, "x": float(x), "y": float(y)}
                for label, (x, y) in zip(labels, coords, strict=True)
            ],
            indent=2,
        )
    )
    for path in staging_paths:
        path.unlink()
    staging.rmdir()
    stage(
        "ready",
        requested_count=result.requested_count,
        returned_count=len(result.selected),
        shortfall=max(0, result.requested_count - len(result.selected)),
        can_try_more=len(result.selected) < result.requested_count,
    )
    return run_dir
