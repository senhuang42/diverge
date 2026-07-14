from __future__ import annotations

import json
from collections import defaultdict
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import numpy as np

from .events import TasteEvent
from .features import CandidateContext
from .model import TasteModel, _record_context


def chronological_folds(
    events: list[TasteEvent], minimum_train: int = 4
) -> list[tuple[list[TasteEvent], list[TasteEvent]]]:
    """Expanding folds that never split a batch across train and test."""
    batches: dict[str, list[TasteEvent]] = defaultdict(list)
    order: list[str] = []
    for event in sorted(events, key=lambda item: item.ts):
        key = event.batch_id or f"event:{event.event_id}"
        if key not in batches:
            order.append(key)
        batches[key].append(event)
    folds = []
    for index in range(1, len(order)):
        train = [event for key in order[:index] for event in batches[key]]
        test = batches[order[index]]
        if len(train) >= minimum_train:
            folds.append((train, test))
    return folds


def _brier(model: TasteModel, events: list[TasteEvent]) -> list[float]:
    scores: list[float] = []
    for event in events:
        if event.event_type != "absolute" or not event.candidate_a:
            continue
        prediction = model.score([_record_context(event.candidate_a, event)])
        observed = float(event.label in {"love", "keep", "export"})
        scores.append((float(prediction.mean[0]) - observed) ** 2)
    return scores


def _pairwise_accuracy(model: TasteModel, events: list[TasteEvent]) -> list[float]:
    output = []
    for event in events:
        if event.event_type != "pairwise" or not event.candidate_a or not event.candidate_b:
            continue
        contexts: list[CandidateContext] = [
            _record_context(event.candidate_a, event),
            _record_context(event.candidate_b, event),
        ]
        means = model.score(contexts).mean
        if event.label == "prefer_a":
            output.append(float(means[0] > means[1]))
        elif event.label == "prefer_b":
            output.append(float(means[1] > means[0]))
    return output


def evaluate_events(
    events: list[TasteEvent], reports_dir: str | Path = "taste/reports"
) -> dict[str, Any]:
    brier: list[float] = []
    pairwise: list[float] = []
    folds = chronological_folds(events)
    for train, test in folds:
        model = TasteModel()
        model.fit(train)
        brier.extend(_brier(model, test))
        pairwise.extend(_pairwise_accuracy(model, test))
    report: dict[str, Any] = {
        "schema_version": 2,
        "evaluation": "chronological-held-out",
        "folds": len(folds),
        "absolute_predictions": len(brier),
        "pairwise_predictions": len(pairwise),
        "brier_score": float(np.mean(brier)) if brier else None,
        "pairwise_accuracy": float(np.mean(pairwise)) if pairwise else None,
        "baselines": ["neutral", "v1_logistic", "prototype", "prototype_pairwise", "contextual"],
    }
    target = Path(reports_dir)
    target.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    (target / f"evaluation-{stamp}.json").write_text(json.dumps(report, indent=2))
    return report
