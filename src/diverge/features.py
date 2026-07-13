from __future__ import annotations

import librosa
import numpy as np

from .audio_io import SAMPLE_RATE, mono

HOP_LENGTH = 512


def _unit(values: np.ndarray) -> np.ndarray:
    values = np.nan_to_num(np.asarray(values, dtype=np.float32))
    maximum = float(np.max(np.abs(values))) if values.size else 0.0
    return values / maximum if maximum > 0 else values


def onset_envelope(y: np.ndarray, sr: int = SAMPLE_RATE) -> np.ndarray:
    return _unit(librosa.onset.onset_strength(y=mono(y), sr=sr, hop_length=HOP_LENGTH))


def chroma(y: np.ndarray, sr: int = SAMPLE_RATE) -> np.ndarray:
    signal = mono(y)
    if not np.any(signal):
        return np.zeros((12, max(1, len(signal) // HOP_LENGTH + 1)), dtype=np.float32)
    return np.nan_to_num(
        librosa.feature.chroma_cqt(y=signal, sr=sr, hop_length=HOP_LENGTH).astype(np.float32)
    )


def groove_similarity(a: np.ndarray, b: np.ndarray, sr: int = SAMPLE_RATE) -> float:
    x, y = onset_envelope(a, sr), onset_envelope(b, sr)
    length = min(len(x), len(y))
    if length == 0:
        return 0.0
    x, y = x[:length], y[:length]
    max_lag = max(1, round(0.120 * sr / HOP_LENGTH))
    scores: list[float] = []
    for lag in range(-max_lag, max_lag + 1):
        xa, ya = (x[-lag:], y[: length + lag]) if lag < 0 else (x[: length - lag], y[lag:])
        if len(xa) < 2:
            continue
        denom = float(np.linalg.norm(xa) * np.linalg.norm(ya))
        scores.append(float(np.dot(xa, ya) / denom) if denom else 0.0)
    return float(np.clip(max(scores, default=0.0), 0.0, 1.0))


def _cosine(a: np.ndarray, b: np.ndarray) -> float:
    denom = float(np.linalg.norm(a) * np.linalg.norm(b))
    return float(np.dot(a, b) / denom) if denom else 0.0


def melody_similarity(a: np.ndarray, b: np.ndarray, sr: int = SAMPLE_RATE) -> float:
    ca, cb = chroma(a, sr), chroma(b, sr)
    global_score = _cosine(ca.mean(axis=1), cb.mean(axis=1))
    frames = min(ca.shape[1], cb.shape[1])
    if frames == 0:
        return 0.0
    indexes_a = np.linspace(0, ca.shape[1] - 1, frames).round().astype(int)
    indexes_b = np.linspace(0, cb.shape[1] - 1, frames).round().astype(int)
    frame_scores = [_cosine(ca[:, i], cb[:, j]) for i, j in zip(indexes_a, indexes_b, strict=True)]
    return float(np.clip(0.5 * global_score + 0.5 * np.mean(frame_scores), 0.0, 1.0))


def timbre_similarity(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.clip((_cosine(np.asarray(a), np.asarray(b)) + 1.0) / 2.0, 0.0, 1.0))
