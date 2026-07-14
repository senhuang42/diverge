from __future__ import annotations

import math
import os
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

import joblib
import numpy as np
from sklearn.cluster import KMeans
from sklearn.metrics import silhouette_score

from .events import CandidateRecord, TasteEvent, effective_events
from .features import CandidateContext, FeatureTransform

MODEL_SCHEMA_VERSION = 2
LABEL_WEIGHTS = {"love": 2.0, "keep": 1.0, "discard": 1.0, "export": 2.5}


@dataclass
class TastePrediction:
    mean: np.ndarray
    uncertainty: np.ndarray
    evidence: np.ndarray
    mode_id: list[str | None]
    factors: list[list[str]]

    def __len__(self) -> int:
        return len(self.mean)


@dataclass
class TasteReport:
    observations: int
    effective_observations: float
    positive_modes: int
    negative_modes: int
    pairwise_observations: int
    confidence: float
    metrics: dict[str, float] = field(default_factory=dict)


class TasteModelProtocol(Protocol):
    def fit(self, events: list[TasteEvent]) -> TasteReport: ...

    def score(self, contexts: list[CandidateContext]) -> TastePrediction: ...


def _sigmoid(value: np.ndarray | float) -> np.ndarray | float:
    clipped = np.clip(value, -30, 30)
    return 1 / (1 + np.exp(-clipped))


def _normalized_rows(values: np.ndarray) -> np.ndarray:
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return values.reshape(0, values.shape[-1] if values.ndim == 2 else 0)
    norms = np.linalg.norm(values, axis=1, keepdims=True)
    return values / np.maximum(norms, 1e-12)


def _record_context(record: CandidateRecord, event: TasteEvent) -> CandidateContext:
    features = record.features or {}
    source = features.get("source_embedding")
    refs = features.get("reference_embeddings", [])
    config = event.run_config or {}
    return CandidateContext(
        candidate_embedding=np.asarray(record.embedding, dtype=np.float32),
        source_embedding=np.asarray(source, dtype=np.float32) if source is not None else None,
        reference_embeddings=np.asarray(refs, dtype=np.float32),
        reference_weights=features.get("reference_weights"),
        scores=dict(features.get("scores") or {}),
        spectral=dict(features.get("spectral") or {}),
        temporal=dict(features.get("temporal") or {}),
        transform=float(config.get("transform", 0)),
        spread=float(config.get("spread", 0)),
        drift=float(config.get("drift", 0)),
        locks=set(config.get("locks") or []),
        source_category=features.get("source_category"),
        path=record.path,
    )


def _clusters(values: np.ndarray, weights: np.ndarray, prefix: str) -> tuple[np.ndarray, list[str]]:
    if len(values) == 0:
        return np.empty((0, values.shape[1] if values.ndim == 2 else 0)), []
    values = _normalized_rows(values)
    if len(values) < 6:
        center = np.average(values, axis=0, weights=weights)
        center /= max(float(np.linalg.norm(center)), 1e-12)
        return center[None, :], [f"{prefix}-1"]
    max_k = min(4, len(values) // 3)
    best_labels = np.zeros(len(values), dtype=int)
    best_score = 0.18  # require meaningful separation before splitting a mode
    for k in range(2, max_k + 1):
        model = KMeans(n_clusters=k, random_state=0, n_init=10).fit(values, sample_weight=weights)
        if len(np.unique(model.labels_)) < 2:
            continue
        score = float(silhouette_score(values, model.labels_, metric="cosine"))
        if score > best_score + 0.03:
            best_score, best_labels = score, model.labels_
    centers = []
    for cluster in sorted(np.unique(best_labels)):
        mask = best_labels == cluster
        center = np.average(values[mask], axis=0, weights=weights[mask])
        center /= max(float(np.linalg.norm(center)), 1e-12)
        centers.append(center)
    return np.asarray(centers), [f"{prefix}-{index + 1}" for index in range(len(centers))]


class TasteModel:
    """Few-shot prototype and Bradley-Terry ensemble with deterministic uncertainty."""

    def __init__(self, transform: FeatureTransform | None = None) -> None:
        self.transform = transform or FeatureTransform()
        self.positive_modes = np.empty((0, self.transform.dimension), dtype=np.float64)
        self.negative_modes = np.empty((0, self.transform.dimension), dtype=np.float64)
        self.positive_mode_ids: list[str] = []
        self.negative_mode_ids: list[str] = []
        self.ranker_coef = np.zeros(self.transform.dimension, dtype=np.float64)
        self.ranker_covariance = np.eye(self.transform.dimension, dtype=np.float64)
        self.pairwise_count = 0
        self.observation_count = 0
        self.effective_count = 0.0
        self.source_counts: dict[str, int] = {}
        self.event_watermark: str | None = None
        self.metrics: dict[str, float] = {}

    def fit(self, events: list[TasteEvent]) -> TasteReport:
        events = effective_events(events)
        positive_x: list[np.ndarray] = []
        positive_w: list[float] = []
        negative_x: list[np.ndarray] = []
        negative_w: list[float] = []
        pair_x: list[np.ndarray] = []
        pair_w: list[float] = []
        self.source_counts = {}
        for event in events:
            if event.candidate_a is None:
                continue
            a = self.transform.transform(_record_context(event.candidate_a, event)).values
            category = str((event.candidate_a.features or {}).get("source_category") or "unknown")
            self.source_counts[category] = self.source_counts.get(category, 0) + 1
            if event.event_type in {"absolute", "export"}:
                weight = LABEL_WEIGHTS.get(event.label, 1.0) * event.strength
                if event.label in {"love", "keep", "export"}:
                    positive_x.append(a)
                    positive_w.append(weight)
                elif event.label == "discard":
                    negative_x.append(a)
                    negative_w.append(weight)
            elif event.event_type == "pairwise" and event.candidate_b is not None:
                b = self.transform.transform(_record_context(event.candidate_b, event)).values
                if event.label == "prefer_a":
                    pair_x.append(a - b)
                    pair_w.append(event.strength)
                elif event.label == "prefer_b":
                    pair_x.append(b - a)
                    pair_w.append(event.strength)
                elif event.label == "neither":
                    negative_x.extend([a, b])
                    negative_w.extend([0.5 * event.strength, 0.5 * event.strength])

        dimension = self.transform.dimension
        px = np.asarray(positive_x, dtype=np.float64).reshape(-1, dimension)
        nx = np.asarray(negative_x, dtype=np.float64).reshape(-1, dimension)
        pw = np.asarray(positive_w, dtype=np.float64)
        nw = np.asarray(negative_w, dtype=np.float64)
        self.positive_modes, self.positive_mode_ids = _clusters(px, pw, "positive")
        self.negative_modes, self.negative_mode_ids = _clusters(nx, nw, "negative")
        self.pairwise_count = len(pair_x)
        self.ranker_coef = np.zeros(dimension, dtype=np.float64)
        regularization = 12.0
        if pair_x:
            x = np.asarray(pair_x, dtype=np.float64)
            weights = np.asarray(pair_w, dtype=np.float64)
            coef = self.ranker_coef
            for _ in range(30):
                probabilities = np.asarray(_sigmoid(x @ coef))
                gradient = x.T @ (weights * (1 - probabilities)) - regularization * coef
                curvature = weights * probabilities * (1 - probabilities)
                hessian = x.T @ (curvature[:, None] * x) + regularization * np.eye(dimension)
                step = np.linalg.solve(hessian, gradient)
                coef += step
                if float(np.linalg.norm(step)) < 1e-7:
                    break
            self.ranker_coef = coef
            self.ranker_covariance = np.linalg.inv(hessian)
        else:
            self.ranker_covariance = np.eye(dimension) / regularization
        self.observation_count = len(positive_x) + len(negative_x) + len(pair_x)
        self.effective_count = float(sum(positive_w) + sum(negative_w) + sum(pair_w))
        self.event_watermark = events[-1].event_id if events else None
        confidence = 1 - math.exp(-self.effective_count / 8)
        return TasteReport(
            observations=self.observation_count,
            effective_observations=self.effective_count,
            positive_modes=len(self.positive_modes),
            negative_modes=len(self.negative_modes),
            pairwise_observations=self.pairwise_count,
            confidence=confidence,
            metrics=self.metrics.copy(),
        )

    def score(self, contexts: list[CandidateContext]) -> TastePrediction:
        count = len(contexts)
        if count == 0:
            empty = np.empty(0, dtype=np.float32)
            return TastePrediction(empty, empty, empty, [], [])
        x = _normalized_rows(self.transform.batch(contexts))
        if self.observation_count == 0:
            return TastePrediction(
                np.full(count, 0.5, dtype=np.float32),
                np.ones(count, dtype=np.float32),
                np.zeros(count, dtype=np.float32),
                [None] * count,
                [["no preference evidence yet"] for _ in contexts],
            )
        pos = x @ self.positive_modes.T if len(self.positive_modes) else np.zeros((count, 0))
        neg = x @ self.negative_modes.T if len(self.negative_modes) else np.zeros((count, 0))
        best_pos = pos.max(axis=1) if pos.shape[1] else np.zeros(count)
        best_neg = neg.max(axis=1) if neg.shape[1] else np.zeros(count)
        balanced = bool(pos.shape[1] and neg.shape[1])
        evidence_ramp = 1 - math.exp(-self.effective_count / 6)
        class_factor = 1.0 if balanced else 0.45
        prototype_logit = 3.0 * (best_pos - best_neg) * evidence_ramp * class_factor
        prototype = np.asarray(_sigmoid(prototype_logit))
        ranker = np.asarray(_sigmoid(x @ self.ranker_coef))
        rank_blend = min(0.45, self.pairwise_count / 18)
        mean = (1 - rank_blend) * prototype + rank_blend * ranker

        relevance = np.maximum(best_pos, best_neg)
        relevance = np.clip((relevance + 1) / 2, 0, 1)
        relevant_evidence = self.effective_count * (0.25 + 0.75 * relevance)
        variance = np.einsum("ij,jk,ik->i", x, self.ranker_covariance, x)
        parameter_uncertainty = np.sqrt(np.maximum(variance, 0)) / (
            1 + np.sqrt(np.maximum(variance, 0))
        )
        uncertainty = np.exp(-relevant_evidence / 6)
        if self.pairwise_count:
            uncertainty = 0.7 * uncertainty + 0.3 * parameter_uncertainty
        for index, context in enumerate(contexts):
            category_count = self.source_counts.get(context.source_category or "unknown", 0)
            if context.source_category and category_count < 2:
                uncertainty[index] = max(uncertainty[index], 0.65)
        if not balanced:
            uncertainty = np.maximum(uncertainty, 0.45)
        uncertainty = np.clip(uncertainty, 0, 1)
        modes: list[str | None] = []
        factors: list[list[str]] = []
        for index in range(count):
            mode = self.positive_mode_ids[int(np.argmax(pos[index]))] if pos.shape[1] else None
            modes.append(mode)
            notes = []
            if best_pos[index] > 0.3:
                notes.append(f"similar to {mode or 'positive examples'}")
            if best_neg[index] > 0.3:
                notes.append("also resembles discarded examples")
            if (
                contexts[index].source_category
                and self.source_counts.get(contexts[index].source_category, 0) < 2
            ):
                notes.append("unfamiliar source context")
            factors.append(notes or ["limited relevant evidence"])
        return TastePrediction(
            mean=np.asarray(mean, dtype=np.float32),
            uncertainty=np.asarray(uncertainty, dtype=np.float32),
            evidence=np.asarray(relevant_evidence, dtype=np.float32),
            mode_id=modes,
            factors=factors,
        )

    def artifact(self) -> dict[str, Any]:
        return {
            "schema_version": MODEL_SCHEMA_VERSION,
            "model": self,
            "feature_transform": self.transform.metadata(),
            "event_watermark": self.event_watermark,
            "metrics": self.metrics,
            "compatibility": {"numpy": np.__version__, "implementation": "taste-v2"},
        }

    def save(self, path: str | Path = "taste/model.joblib") -> Path:
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(prefix=f".{target.name}.", dir=target.parent)
        os.close(descriptor)
        try:
            joblib.dump(self.artifact(), temporary)
            os.replace(temporary, target)
        finally:
            Path(temporary).unlink(missing_ok=True)
        return target

    @classmethod
    def load(cls, path: str | Path = "taste/model.joblib") -> TasteModel:
        artifact = joblib.load(path)
        if not isinstance(artifact, dict) or artifact.get("schema_version") != MODEL_SCHEMA_VERSION:
            raise ValueError("incompatible taste model artifact; neutral scoring will be used")
        model = artifact.get("model")
        if not isinstance(model, cls):
            raise ValueError("taste artifact does not contain a compatible model")
        return model


def load_or_neutral(path: str | Path | None) -> tuple[TasteModel, str | None]:
    if path is None or not Path(path).exists():
        return TasteModel(), None
    try:
        return TasteModel.load(path), None
    except Exception as exc:  # corrupted/incompatible local state must never stop generation
        return TasteModel(), str(exc)
