from __future__ import annotations

import librosa
import numpy as np

from .audio_io import SAMPLE_RATE, mono

HOP_LENGTH = 512

_KEY_PROFILES = {
    "major": (0, 2, 4, 5, 7, 9, 11),
    "minor": (0, 2, 3, 5, 7, 8, 10),
}
_PITCH_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


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
    return groove_similarity_envelopes(onset_envelope(a, sr), onset_envelope(b, sr), sr)


def groove_similarity_envelopes(x: np.ndarray, y: np.ndarray, sr: int = SAMPLE_RATE) -> float:
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
    return melody_similarity_chroma(chroma(a, sr), chroma(b, sr))


def melody_similarity_chroma(ca: np.ndarray, cb: np.ndarray) -> float:
    global_score = _cosine(ca.mean(axis=1), cb.mean(axis=1))
    frames = min(ca.shape[1], cb.shape[1])
    if frames == 0:
        return 0.0
    indexes_a = np.linspace(0, ca.shape[1] - 1, frames).round().astype(int)
    indexes_b = np.linspace(0, cb.shape[1] - 1, frames).round().astype(int)
    frame_scores = [_cosine(ca[:, i], cb[:, j]) for i, j in zip(indexes_a, indexes_b, strict=True)]
    return float(np.clip(0.5 * global_score + 0.5 * np.mean(frame_scores), 0.0, 1.0))


def tonal_coherence_chroma(values: np.ndarray) -> dict[str, float | str | bool]:
    """Score tonal organization relative to a candidate's own inferred key.

    A source-comparison metric mistakes an intentionally new key or melody for incoherence. This
    metric instead finds the best-fitting major or minor pitch set, then measures both pitch-class
    concentration and short-term harmonic continuity inside the candidate itself.
    """
    candidate = np.nan_to_num(np.asarray(values, dtype=np.float32), posinf=0.0, neginf=0.0)
    if candidate.ndim != 2 or candidate.shape[0] != 12:
        raise ValueError("chroma must have shape (12, frames)")
    energy = np.maximum(candidate, 0.0)
    total = float(energy.sum())
    if total <= 1e-8:
        return {
            "applicable": False,
            "key": "unknown",
            "scale_fit": 0.0,
            "harmonic_continuity": 0.0,
            "score": 0.0,
        }

    pitch_energy = energy.sum(axis=1)
    best_root = 0
    best_mode = "major"
    best_fit = -1.0
    best_indexes: np.ndarray | None = None
    for mode, intervals in _KEY_PROFILES.items():
        for root in range(12):
            indexes = np.asarray([(root + interval) % 12 for interval in intervals])
            fit = float(pitch_energy[indexes].sum() / total)
            if fit > best_fit:
                best_root, best_mode, best_fit, best_indexes = root, mode, fit, indexes

    # Uniform chroma puts 7/12 of its energy in every diatonic scale. Treat that as zero evidence
    # of tonality and normalize a perfect scale fit to one.
    uniform_fit = 7 / 12
    scale_fit = float(np.clip((best_fit - uniform_fit) / (1 - uniform_fit), 0.0, 1.0))
    frame_totals = energy.sum(axis=0)
    active = frame_totals > 1e-8
    continuity_scores: list[float] = []
    if np.count_nonzero(active) >= 2:
        normalized = energy[:, active] / frame_totals[active]
        for left, right in zip(normalized.T[:-1], normalized.T[1:], strict=True):
            continuity_scores.append(_cosine(left, right))
    continuity = float(np.clip(np.mean(continuity_scores), 0.0, 1.0)) if continuity_scores else 1.0
    score = float(np.clip(0.7 * scale_fit + 0.3 * continuity, 0.0, 1.0))
    assert best_indexes is not None
    return {
        "applicable": True,
        "key": f"{_PITCH_NAMES[best_root]} {best_mode}",
        "scale_fit": round(scale_fit, 6),
        "harmonic_continuity": round(continuity, 6),
        "score": round(score, 6),
    }


def tonal_coherence(y: np.ndarray, sr: int = SAMPLE_RATE) -> dict[str, float | str | bool]:
    return tonal_coherence_chroma(chroma(y, sr))


def timbre_similarity(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.clip((_cosine(np.asarray(a), np.asarray(b)) + 1.0) / 2.0, 0.0, 1.0))
