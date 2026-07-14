from __future__ import annotations

import json
from datetime import UTC, datetime
from pathlib import Path

import numpy as np

from .audio_io import load_audio, save_audio
from .config import RunConfig
from .critic import taste_scores
from .embed import Embedder
from .generator import GeneratorProtocol, transform_to_noise
from .locks import active_lock_score, lock_similarities, prepare_lock_source
from .map2d import project_2d
from .novelty import novelty_scores, recent_kept_embeddings, self_novelty_scores
from .select import Candidate, select_candidates
from .taste.events import TasteEventStore
from .taste.features import CandidateContext, audio_descriptors, descriptor_scores
from .taste.model import load_or_neutral
from .taste.profile import enrich_prompt, infer_descriptors


def _style_hint(config: RunConfig) -> str:
    if config.style_text_hint.strip():
        return config.style_text_hint.strip()
    ignored = {"a", "audio", "ref", "reference", "sample", "track"}
    names = []
    for path, _ in config.references:
        words = path.stem.lower().replace("_", " ").replace("-", " ").split()
        useful = [word for word in words if word not in ignored and not word.isdigit()]
        if useful:
            names.append(" ".join(useful))
    details = f", {', '.join(names)}" if names else ""
    return f"clean polished instrumental production loop{details}"


def run_session(
    config: RunConfig,
    generator: GeneratorProtocol,
    embedder: Embedder,
    *,
    progress=print,
) -> Path:
    source, sr = load_audio(config.source)
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
        style_embedding = embedder.embed_file(config.source)
    if hasattr(generator, "progress"):
        generator.progress = progress
    generated = generator.generate(
        source,
        sr,
        style_embedding,
        prompt,
        config.transform,
        config.duration_s,
        config.seed,
        config.n_oversample,
    )
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    run_dir = config.output_dir / stamp
    staging = run_dir / ".oversample"
    staging.mkdir(parents=True, exist_ok=False)
    staging_paths = []
    for index, audio in enumerate(generated):
        if not getattr(generator, "emits_progress", False):
            progress(f"PROGRESS {index + 1}/{config.n_oversample}")
        staging_paths.append(save_audio(staging / f"raw_{index:03d}.wav", audio, sr))
    candidate_embeddings = embedder.embed_batch(staging_paths)
    source_embedding = embedder.embed_file(config.source)
    source_lock_features = prepare_lock_source(source, source_embedding, sr)
    ref_fit = np.clip((candidate_embeddings @ style_embedding + 1) / 2, 0, 1)
    novelty = novelty_scores(candidate_embeddings, config.library_index)
    self_novelty = self_novelty_scores(
        candidate_embeddings, recent_kept_embeddings(config.choices_path)
    )
    legacy_taste = taste_scores(candidate_embeddings, config.critic_model)
    candidates = []
    contexts = []
    candidate_similarities = []
    for index, (audio, embedding) in enumerate(zip(generated, candidate_embeddings, strict=True)):
        similarities = lock_similarities(
            audio,
            source,
            embedding,
            source_embedding,
            sr,
            source_features=source_lock_features,
        )
        candidate_similarities.append(similarities)
        spectral, temporal = audio_descriptors(audio, sr)
        contexts.append(
            CandidateContext(
                candidate_embedding=embedding,
                source_embedding=source_embedding,
                reference_embeddings=reference_embeddings,
                reference_weights=[weight for _, weight in config.references],
                scores={
                    "groove": similarities.get("groove", 0.0),
                    "melody": similarities.get("melody", 0.0),
                    "timbre": similarities.get("timbre", 0.0),
                    "novelty": float(novelty[index]),
                    "self_novelty": float(self_novelty[index]),
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
    predictions = taste_model.score(contexts)
    for index, embedding in enumerate(candidate_embeddings):
        similarities = candidate_similarities[index]
        taste_mean = (
            float(predictions.mean[index])
            if taste_model.observation_count
            else float(legacy_taste[index])
        )
        candidates.append(
            Candidate(
                index=index,
                embedding=embedding,
                ref_fit=float(ref_fit[index]),
                taste=taste_mean,
                novelty=float(novelty[index]),
                self_novelty=float(self_novelty[index]),
                locks=similarities,
                lock_score=active_lock_score(similarities, config.locks),
                taste_uncertainty=float(predictions.uncertainty[index]),
                taste_evidence=float(predictions.evidence[index]),
                taste_mode=predictions.mode_id[index],
                taste_factors=predictions.factors[index],
            )
        )
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
                "seed": config.seed + candidate.index,
                "ref_fit": round(candidate.ref_fit, 6),
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
            }
        )
    manifest = {
        "config": config.to_dict(),
        "model_ids": {"embedder": embedder.model_id, "generator": type(generator).__name__},
        "generator_settings": getattr(generator, "inference_settings", {}),
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
            "utility_weights": result.weights,
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
    all_embeddings = np.vstack([source_embedding, reference_embeddings, winner_embeddings])
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
    return run_dir
