from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .events import TasteEventStore
from .model import TasteModel

DESCRIPTORS: dict[str, tuple[str, ...]] = {
    "production": ("dry", "polished", "raw", "compressed"),
    "timbral": ("dark", "bright", "warm", "metallic"),
    "rhythmic": ("percussive", "syncopated", "steady", "sparse"),
    "density": ("minimal", "dense", "layered"),
    "spatial": ("intimate", "wide", "reverberant"),
}


def infer_descriptors(events: list[Any], minimum_support: float = 2.0) -> list[dict[str, Any]]:
    """Infer only descriptors explicitly attached by local feature extraction."""
    positive: dict[str, float] = {}
    negative: dict[str, float] = {}
    allowed = {item for values in DESCRIPTORS.values() for item in values}
    for event in events:
        if not getattr(event, "candidate_a", None):
            continue
        descriptor_scores = event.candidate_a.features.get("descriptors", {})
        target = negative if event.label == "discard" else positive
        if event.label not in {"love", "keep", "export", "discard"}:
            continue
        for descriptor, value in descriptor_scores.items():
            if descriptor in allowed:
                target[descriptor] = target.get(descriptor, 0.0) + float(value) * event.strength
    hypotheses = []
    for descriptor, support in positive.items():
        contrast = support - negative.get(descriptor, 0.0)
        if support >= minimum_support and contrast > 0:
            hypotheses.append(
                {
                    "descriptor": descriptor,
                    "confidence": min(
                        0.95, contrast / (support + negative.get(descriptor, 0.0) + 1)
                    ),
                    "positive_support": support,
                    "negative_support": negative.get(descriptor, 0.0),
                    "phrase": f"often favors: {descriptor}",
                }
            )
    return sorted(hypotheses, key=lambda item: (-item["confidence"], item["descriptor"]))


def profile_dict(
    model: TasteModel, events_path: str | Path = "taste/events.jsonl"
) -> dict[str, Any]:
    events = TasteEventStore(events_path).load(effective=True)
    representatives: dict[str, list[str]] = {mode: [] for mode in model.positive_mode_ids}
    discarded: list[str] = []
    for event in events:
        if not event.candidate_a:
            continue
        if event.label in {"love", "keep", "export"} and model.positive_mode_ids:
            representatives[model.positive_mode_ids[0]].append(event.candidate_a.path)
        elif event.label == "discard":
            discarded.append(event.candidate_a.path)
    return {
        "schema_version": 2,
        "evidence": model.effective_count,
        "confidence": 1 - __import__("math").exp(-model.effective_count / 8),
        "positive_modes": [
            {"id": mode, "representatives": representatives.get(mode, [])[:3]}
            for mode in model.positive_mode_ids
        ],
        "negative_modes": [
            {"id": mode, "representatives": discarded[:3]} for mode in model.negative_mode_ids
        ],
        "source_categories": model.source_counts,
        "event_watermark": model.event_watermark,
        "language": "Inferred descriptors are editable hypotheses, not facts.",
    }


def export_profile(
    model_path: str | Path = "taste/model.joblib",
    events_path: str | Path = "taste/events.jsonl",
    output_path: str | Path = "taste/profile.json",
) -> Path:
    model = TasteModel.load(model_path)
    target = Path(output_path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(profile_dict(model, events_path), indent=2))
    return target


def enrich_prompt(
    prompt: str,
    hypotheses: list[dict[str, Any]],
    *,
    evidence: float,
    confidence: float,
    explicit_style_hint: bool = False,
    enabled: bool = True,
) -> tuple[str, list[str]]:
    if not enabled or explicit_style_hint or evidence < 6 or confidence < 0.6:
        return prompt, []
    allowed = {item for values in DESCRIPTORS.values() for item in values}
    additions = [
        str(item["descriptor"])
        for item in sorted(hypotheses, key=lambda item: -float(item.get("confidence", 0)))
        if item.get("descriptor") in allowed and float(item.get("confidence", 0)) >= 0.6
    ][:3]
    return (f"{prompt}, {', '.join(additions)}" if additions else prompt), additions
