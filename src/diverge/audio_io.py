from __future__ import annotations

from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

SAMPLE_RATE = 44_100


def _stereo(audio: np.ndarray) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float32)
    if audio.ndim == 1:
        return np.stack([audio, audio], axis=0)
    if audio.ndim != 2:
        raise ValueError("audio must have one or two dimensions")
    if audio.shape[0] not in (1, 2) and audio.shape[1] in (1, 2):
        audio = audio.T
    if audio.shape[0] == 1:
        audio = np.repeat(audio, 2, axis=0)
    elif audio.shape[0] > 2:
        audio = np.stack([audio[::2].mean(axis=0), audio[1::2].mean(axis=0)])
    return audio.astype(np.float32, copy=False)


def normalize(audio: np.ndarray, peak: float = 0.98) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float32)
    maximum = float(np.max(np.abs(audio))) if audio.size else 0.0
    if maximum == 0 or maximum <= peak:
        return audio
    return audio * (peak / maximum)


def load_audio(path: str | Path, target_sr: int = SAMPLE_RATE) -> tuple[np.ndarray, int]:
    audio, sr = sf.read(Path(path), dtype="float32", always_2d=True)
    audio = _stereo(audio.T)
    if sr != target_sr:
        from math import gcd

        factor = gcd(sr, target_sr)
        audio = resample_poly(audio, target_sr // factor, sr // factor, axis=-1).astype(np.float32)
    return audio, target_sr


def save_audio(path: str | Path, audio: np.ndarray, sr: int = SAMPLE_RATE) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, normalize(_stereo(audio)).T, sr, subtype="FLOAT", format="WAV")
    return path


def mono(audio: np.ndarray) -> np.ndarray:
    return _stereo(audio).mean(axis=0)
