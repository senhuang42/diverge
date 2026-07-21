from __future__ import annotations

import json
from collections import defaultdict
from dataclasses import replace
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Protocol

import numpy as np
from sklearn.linear_model import LogisticRegression

from .events import CandidateRecord, TasteEvent
from .features import CandidateContext
from .model import TasteModel, TastePrediction, _record_context
from .synthetic import all_profiles

TARGET_NDCG = 0.8
POSITIVE_LABELS = {"love", "keep", "export"}
MODEL_NAMES = ("neutral", "v1_logistic", "prototype", "prototype_pairwise", "contextual")


class EvaluationModel(Protocol):
    def fit(self, events: list[TasteEvent]) -> Any: ...

    def score(self, contexts: list[CandidateContext]) -> TastePrediction: ...


def chronological_folds(
    events: list[TasteEvent], minimum_train: int = 4
) -> list[tuple[list[TasteEvent], list[TasteEvent]]]:
    """Return expanding folds without putting one batch in both train and test."""
    batches: dict[str, list[TasteEvent]] = defaultdict(list)
    order: list[str] = []
    for event in sorted(events, key=lambda item: (item.ts, item.event_id)):
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


def _empty_prediction(means: np.ndarray, uncertainty: float) -> TastePrediction:
    count = len(means)
    return TastePrediction(
        mean=np.asarray(means, dtype=np.float32),
        uncertainty=np.full(count, uncertainty, dtype=np.float32),
        evidence=np.zeros(count, dtype=np.float32),
        mode_id=[None] * count,
        factors=[["evaluation baseline"] for _ in range(count)],
    )


class NeutralBaseline:
    def fit(self, events: list[TasteEvent]) -> None:
        del events

    def score(self, contexts: list[CandidateContext]) -> TastePrediction:
        return _empty_prediction(np.full(len(contexts), 0.5), 1.0)


class V1LogisticBaseline:
    """Legacy embedding-only logistic classifier with neutral few-shot fallback."""

    def __init__(self) -> None:
        self.model: LogisticRegression | None = None

    def fit(self, events: list[TasteEvent]) -> None:
        usable = [
            event
            for event in events
            if event.event_type in {"absolute", "export"}
            and event.candidate_a
            and event.label in POSITIVE_LABELS | {"discard"}
        ]
        labels = np.asarray([event.label in POSITIVE_LABELS for event in usable], dtype=np.int8)
        if len(np.unique(labels)) < 2:
            self.model = None
            return
        embeddings = np.asarray([event.candidate_a.embedding for event in usable], dtype=np.float32)
        self.model = LogisticRegression(max_iter=1_000, random_state=0).fit(embeddings, labels)

    def score(self, contexts: list[CandidateContext]) -> TastePrediction:
        if not contexts or self.model is None:
            return _empty_prediction(np.full(len(contexts), 0.5), 1.0)
        embeddings = np.asarray(
            [context.candidate_embedding for context in contexts], dtype=np.float32
        )
        means = self.model.predict_proba(embeddings)[:, 1]
        uncertainty = 1 - np.abs(means - 0.5) * 2
        return _empty_prediction(means, float(np.mean(uncertainty)))


def _embedding_only_record(record: CandidateRecord | None) -> CandidateRecord | None:
    return replace(record, features={}) if record is not None else None


def _embedding_only_event(event: TasteEvent) -> TasteEvent:
    return replace(
        event,
        candidate_a=_embedding_only_record(event.candidate_a),
        candidate_b=_embedding_only_record(event.candidate_b),
        run_config={},
        source_path=None,
        source_embedding_hash=None,
        reference_embedding_hashes=[],
    )


class TasteBaseline:
    def __init__(self, *, pairwise: bool, contextual: bool) -> None:
        self.pairwise = pairwise
        self.contextual = contextual
        self.model = TasteModel()

    def fit(self, events: list[TasteEvent]) -> Any:
        usable = (
            events
            if self.pairwise
            else [event for event in events if event.event_type != "pairwise"]
        )
        if not self.contextual:
            usable = [_embedding_only_event(event) for event in usable]
        return self.model.fit(usable)

    def score(self, contexts: list[CandidateContext]) -> TastePrediction:
        if not self.contextual:
            contexts = [CandidateContext(context.candidate_embedding) for context in contexts]
        return self.model.score(contexts)


def evaluation_models() -> dict[str, EvaluationModel]:
    return {
        "neutral": NeutralBaseline(),
        "v1_logistic": V1LogisticBaseline(),
        "prototype": TasteBaseline(pairwise=False, contextual=False),
        "prototype_pairwise": TasteBaseline(pairwise=True, contextual=False),
        "contextual": TasteBaseline(pairwise=True, contextual=True),
    }


def _brier(model: EvaluationModel, events: list[TasteEvent]) -> list[float]:
    scores: list[float] = []
    for event in events:
        if event.event_type not in {"absolute", "export"} or not event.candidate_a:
            continue
        prediction = model.score([_record_context(event.candidate_a, event)])
        observed = float(event.label in POSITIVE_LABELS)
        scores.append((float(prediction.mean[0]) - observed) ** 2)
    return scores


def _pairwise_accuracy(model: EvaluationModel, events: list[TasteEvent]) -> list[float]:
    output = []
    for event in events:
        if event.event_type != "pairwise" or not event.candidate_a or not event.candidate_b:
            continue
        contexts = [
            _record_context(event.candidate_a, event),
            _record_context(event.candidate_b, event),
        ]
        left, right = model.score(contexts).mean
        if event.label == "prefer_a":
            output.append(1.0 if left > right else 0.5 if left == right else 0.0)
        elif event.label == "prefer_b":
            output.append(1.0 if right > left else 0.5 if left == right else 0.0)
        elif event.label == "neither":
            output.append(float(left <= 0.5 and right <= 0.5))
    return output


def _batch_ranking(
    model: EvaluationModel, events: list[TasteEvent]
) -> tuple[list[float], list[float]]:
    by_batch: dict[str, list[TasteEvent]] = defaultdict(list)
    for event in events:
        if event.event_type in {"absolute", "export"} and event.candidate_a:
            by_batch[event.batch_id or event.event_id].append(event)
    hits: list[float] = []
    ndcg: list[float] = []
    for batch in by_batch.values():
        if len(batch) < 2:
            continue
        contexts = [_record_context(item.candidate_a, item) for item in batch]
        means = model.score(contexts).mean
        observed = np.asarray([float(item.label in POSITIVE_LABELS) for item in batch])
        order = np.argsort(-means, kind="stable")
        k = min(3, len(batch))
        hits.append(float(observed[order[:k]].max()))
        discounts = 1 / np.log2(np.arange(2, len(batch) + 2))
        dcg = float(np.sum(observed[order] * discounts))
        ideal = float(np.sum(np.sort(observed)[::-1] * discounts))
        ndcg.append(dcg / ideal if ideal else 1.0)
    return hits, ndcg


def _mean(values: list[float]) -> float | None:
    return float(np.mean(values)) if values else None


def evaluate_model(events: list[TasteEvent], name: str) -> dict[str, float | int | None]:
    if name not in MODEL_NAMES:
        raise ValueError(f"unknown evaluation model: {name}")
    brier: list[float] = []
    pairwise: list[float] = []
    hits: list[float] = []
    ndcg: list[float] = []
    decisions_to_target: int | None = None
    folds = chronological_folds(events)
    for train, test in folds:
        model = evaluation_models()[name]
        model.fit(train)
        brier.extend(_brier(model, test))
        pairwise.extend(_pairwise_accuracy(model, test))
        fold_hits, fold_ndcg = _batch_ranking(model, test)
        hits.extend(fold_hits)
        ndcg.extend(fold_ndcg)
        if decisions_to_target is None and fold_ndcg and float(np.mean(fold_ndcg)) >= TARGET_NDCG:
            decisions_to_target = len(train)
    return {
        "folds": len(folds),
        "absolute_predictions": len(brier),
        "pairwise_predictions": len(pairwise),
        "ranked_batches": len(ndcg),
        "brier_score": _mean(brier),
        "pairwise_accuracy": _mean(pairwise),
        "top_k_hit_rate": _mean(hits),
        "ndcg": _mean(ndcg),
        "decisions_to_ndcg_0_8": decisions_to_target,
    }


def _interval(values: list[float]) -> dict[str, float] | None:
    if not values:
        return None
    return {
        "mean": float(np.mean(values)),
        "low": float(np.percentile(values, 5)),
        "high": float(np.percentile(values, 95)),
    }


def evaluate_events(
    events: list[TasteEvent], reports_dir: str | Path = "taste/reports"
) -> dict[str, Any]:
    models = {name: evaluate_model(events, name) for name in MODEL_NAMES}
    synthetic_results: dict[str, dict[str, Any]] = {}
    interval_values: dict[str, dict[str, list[float]]] = {
        name: defaultdict(list) for name in MODEL_NAMES
    }
    for profile_name, profile_events in all_profiles().items():
        profile_models = {
            name: evaluate_model(profile_events, name) for name in MODEL_NAMES
        }
        for name, metrics in profile_models.items():
            for metric in ("brier_score", "top_k_hit_rate", "ndcg"):
                value = metrics[metric]
                if value is not None:
                    interval_values[name][metric].append(float(value))
        synthetic_results[profile_name] = {
            **profile_models["contextual"],
            "models": profile_models,
        }
    intervals = {
        name: {
            metric: _interval(values)
            for metric, values in interval_values[name].items()
        }
        for name in MODEL_NAMES
    }
    contextual = models["contextual"]
    v1 = models["v1_logistic"]
    brier_lift = None
    if contextual["brier_score"] is not None and v1["brier_score"] is not None:
        brier_lift = float(v1["brier_score"] - contextual["brier_score"])
    ndcg_lift = None
    if contextual["ndcg"] is not None and v1["ndcg"] is not None:
        ndcg_lift = float(contextual["ndcg"] - v1["ndcg"])
    report: dict[str, Any] = {
        "schema_version": 2,
        "evaluation": "chronological-held-out",
        **contextual,
        "neutral_brier": 0.25,
        "models": models,
        "baselines": list(MODEL_NAMES),
        "comparison": {
            "contextual_brier_lift_vs_v1": brier_lift,
            "contextual_ndcg_lift_vs_v1": ndcg_lift,
        },
        "synthetic_profiles": synthetic_results,
        "synthetic_intervals": intervals,
    }
    target = Path(reports_dir)
    target.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    (target / f"evaluation-{stamp}.json").write_text(json.dumps(report, indent=2))
    return report
