from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

import numpy as np

FEATURE_SCHEMA_VERSION = 2
PROJECTION_VERSION = "random-48-v1"
EMBEDDING_SIZE = 512
PROJECTED_SIZE = 48
SCORE_NAMES = ("groove", "melody", "timbre", "novelty", "self_novelty", "lock_score")
SPECTRAL_NAMES = ("centroid", "bandwidth", "rolloff", "flatness", "rms", "crest_factor")
TEMPORAL_NAMES = ("onset_density", "onset_mean", "onset_std", "dynamic_range")
LOCK_NAMES = ("groove", "melody", "timbre")


def _vector(value: np.ndarray | list[float] | None) -> np.ndarray:
    if value is None:
        return np.zeros(EMBEDDING_SIZE, dtype=np.float32)
    result = np.asarray(value, dtype=np.float32).reshape(-1)
    if result.size != EMBEDDING_SIZE:
        raise ValueError("CLAP embeddings must have 512 values")
    return result


def _cosine(a: np.ndarray, b: np.ndarray) -> float:
    denominator = float(np.linalg.norm(a) * np.linalg.norm(b))
    return float(a @ b / denominator) if denominator else 0.0


@dataclass
class CandidateContext:
    candidate_embedding: np.ndarray
    source_embedding: np.ndarray | None = None
    reference_embeddings: np.ndarray | list[np.ndarray] = field(
        default_factory=lambda: np.empty((0, EMBEDDING_SIZE), dtype=np.float32)
    )
    reference_weights: np.ndarray | list[float] | None = None
    scores: dict[str, float] = field(default_factory=dict)
    spectral: dict[str, float] = field(default_factory=dict)
    temporal: dict[str, float] = field(default_factory=dict)
    transform: float = 0.0
    spread: float = 0.0
    drift: float = 0.0
    locks: set[str] = field(default_factory=set)
    source_category: str | None = None
    path: str | None = None


@dataclass
class FeatureVector:
    values: np.ndarray
    groups: dict[str, slice]
    names: list[str]
    schema_version: int = FEATURE_SCHEMA_VERSION


class FeatureTransform:
    """Deterministic grouped feature transform; usable before any observations exist."""

    def __init__(self, seed: int = 20260714, projection: np.ndarray | None = None) -> None:
        if projection is None:
            rng = np.random.default_rng(seed)
            projection = rng.normal(
                0, 1 / np.sqrt(PROJECTED_SIZE), (EMBEDDING_SIZE, PROJECTED_SIZE)
            )
        projection = np.asarray(projection, dtype=np.float32)
        if projection.shape != (EMBEDDING_SIZE, PROJECTED_SIZE):
            raise ValueError("projection must have shape (512, 48)")
        self.projection = projection
        self.seed = seed
        self.version = PROJECTION_VERSION

    def _project(self, vector: np.ndarray) -> np.ndarray:
        result = vector @ self.projection
        norm = float(np.linalg.norm(result))
        return (result / norm if norm else result).astype(np.float32)

    def transform(self, context: CandidateContext) -> FeatureVector:
        candidate = _vector(context.candidate_embedding)
        source = _vector(context.source_embedding)
        refs = np.asarray(context.reference_embeddings, dtype=np.float32)
        if refs.size == 0:
            refs = np.empty((0, EMBEDDING_SIZE), dtype=np.float32)
        if refs.ndim != 2 or refs.shape[1] != EMBEDDING_SIZE:
            raise ValueError("reference_embeddings must have shape (n, 512)")
        has_reference = float(len(refs) > 0)
        if len(refs):
            weights = np.asarray(
                context.reference_weights
                if context.reference_weights is not None
                else np.ones(len(refs)),
                dtype=np.float32,
            )
            if weights.size != len(refs) or float(weights.sum()) <= 0:
                raise ValueError("reference weights must match references and have positive sum")
            weights /= weights.sum()
            reference = weights @ refs
            ref_similarity = max(_cosine(candidate, item) for item in refs)
        else:
            reference = np.zeros(EMBEDDING_SIZE, dtype=np.float32)
            ref_similarity = 0.0

        group_values: list[tuple[str, np.ndarray, list[str]]] = [
            (
                "candidate",
                self._project(candidate),
                [f"candidate_rp_{i}" for i in range(PROJECTED_SIZE)],
            ),
            (
                "source_difference",
                self._project(candidate - source),
                [f"source_diff_rp_{i}" for i in range(PROJECTED_SIZE)],
            ),
            (
                "reference_difference",
                self._project(candidate - reference)
                if has_reference
                else np.zeros(PROJECTED_SIZE, dtype=np.float32),
                [f"reference_diff_rp_{i}" for i in range(PROJECTED_SIZE)],
            ),
            (
                "similarities",
                np.asarray(
                    [_cosine(candidate, source), ref_similarity, has_reference], dtype=np.float32
                ),
                ["candidate_source_cosine", "candidate_reference_cosine", "has_reference"],
            ),
            (
                "scores",
                np.asarray(
                    [context.scores.get(name, 0.0) for name in SCORE_NAMES], dtype=np.float32
                ),
                list(SCORE_NAMES),
            ),
            (
                "spectral",
                np.asarray(
                    [context.spectral.get(name, 0.0) for name in SPECTRAL_NAMES], dtype=np.float32
                ),
                list(SPECTRAL_NAMES),
            ),
            (
                "temporal",
                np.asarray(
                    [context.temporal.get(name, 0.0) for name in TEMPORAL_NAMES], dtype=np.float32
                ),
                list(TEMPORAL_NAMES),
            ),
            (
                "settings",
                np.asarray(
                    [
                        context.transform / 100,
                        context.spread / 100,
                        context.drift / 100,
                        *[float(name in context.locks) for name in LOCK_NAMES],
                        min(len(refs), 8) / 8,
                    ],
                    dtype=np.float32,
                ),
                [
                    "transform",
                    "spread",
                    "drift",
                    *[f"lock_{n}" for n in LOCK_NAMES],
                    "reference_count",
                ],
            ),
        ]
        # Unit-normalize high-dimensional groups independently. Scalar descriptors retain scale.
        for index in range(3):
            name, values, names = group_values[index]
            norm = float(np.linalg.norm(values))
            group_values[index] = (name, values / norm if norm else values, names)
        groups: dict[str, slice] = {}
        names: list[str] = []
        arrays: list[np.ndarray] = []
        cursor = 0
        for group, values, group_names in group_values:
            groups[group] = slice(cursor, cursor + len(values))
            cursor += len(values)
            arrays.append(values)
            names.extend(group_names)
        return FeatureVector(np.concatenate(arrays).astype(np.float32), groups, names)

    def batch(self, contexts: list[CandidateContext]) -> np.ndarray:
        if not contexts:
            return np.empty((0, self.dimension), dtype=np.float32)
        return np.vstack([self.transform(context).values for context in contexts])

    @property
    def dimension(self) -> int:
        dummy = CandidateContext(np.zeros(EMBEDDING_SIZE, dtype=np.float32))
        return int(self.transform(dummy).values.size)

    def metadata(self) -> dict[str, Any]:
        return {
            "feature_schema_version": FEATURE_SCHEMA_VERSION,
            "projection_version": self.version,
            "projection_seed": self.seed,
            "dimension": self.dimension,
        }


def audio_descriptors(
    audio: np.ndarray, sample_rate: int
) -> tuple[dict[str, float], dict[str, float]]:
    """Compact, bounded descriptors computed without any model weights."""
    import librosa

    signal = np.asarray(audio, dtype=np.float32)
    if signal.ndim == 2:
        signal = signal.mean(axis=0)
    if signal.size == 0:
        return ({name: 0.0 for name in SPECTRAL_NAMES}, {name: 0.0 for name in TEMPORAL_NAMES})
    n_fft = min(1024, max(64, signal.size))
    magnitude = np.abs(librosa.stft(signal, n_fft=n_fft))
    rms = librosa.feature.rms(S=magnitude, frame_length=n_fft).reshape(-1)
    onset = librosa.onset.onset_strength(y=signal, sr=sample_rate)
    duration = signal.size / sample_rate
    peaks = librosa.onset.onset_detect(onset_envelope=onset, sr=sample_rate)
    abs_signal = np.abs(signal)
    spectral = {
        "centroid": float(np.mean(librosa.feature.spectral_centroid(S=magnitude, sr=sample_rate)))
        / max(sample_rate / 2, 1),
        "bandwidth": float(np.mean(librosa.feature.spectral_bandwidth(S=magnitude, sr=sample_rate)))
        / max(sample_rate / 2, 1),
        "rolloff": float(np.mean(librosa.feature.spectral_rolloff(S=magnitude, sr=sample_rate)))
        / max(sample_rate / 2, 1),
        "flatness": float(np.mean(librosa.feature.spectral_flatness(S=magnitude))),
        "rms": float(np.clip(np.mean(rms), 0, 1)),
        "crest_factor": float(
            np.clip(abs_signal.max() / max(float(np.sqrt(np.mean(signal**2))), 1e-8) / 20, 0, 1)
        ),
    }
    temporal = {
        "onset_density": float(np.clip(len(peaks) / max(duration, 1e-8) / 20, 0, 1)),
        "onset_mean": float(np.clip(np.mean(onset) / 10, 0, 1)) if onset.size else 0.0,
        "onset_std": float(np.clip(np.std(onset) / 10, 0, 1)) if onset.size else 0.0,
        "dynamic_range": float(
            np.clip((np.percentile(rms, 95) - np.percentile(rms, 5)) / 0.5, 0, 1)
        )
        if rms.size
        else 0.0,
    }
    return spectral, temporal
