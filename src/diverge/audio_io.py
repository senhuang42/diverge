from __future__ import annotations

from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

SAMPLE_RATE = 44_100


def channels_first(audio: np.ndarray) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float32)
    if audio.ndim == 1:
        return audio[np.newaxis, :]
    if audio.ndim != 2:
        raise ValueError("audio must have one or two dimensions")
    if audio.shape[0] not in (1, 2) and audio.shape[1] in (1, 2):
        audio = audio.T
    if audio.shape[0] > 2:
        audio = np.stack([audio[::2].mean(axis=0), audio[1::2].mean(axis=0)])
    return audio.astype(np.float32, copy=False)


def match_channels(audio: np.ndarray, channels: int) -> np.ndarray:
    signal = channels_first(audio)
    if channels == signal.shape[0]:
        return signal
    if channels == 1:
        return signal.mean(axis=0, keepdims=True).astype(np.float32)
    if channels == 2 and signal.shape[0] == 1:
        return np.repeat(signal, 2, axis=0)
    raise ValueError("target channels must be 1 or 2")


def normalize(audio: np.ndarray, peak: float = 0.98) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float32)
    maximum = float(np.max(np.abs(audio))) if audio.size else 0.0
    if maximum == 0 or maximum <= peak:
        return audio
    return audio * (peak / maximum)


def load_audio(path: str | Path, target_sr: int = SAMPLE_RATE) -> tuple[np.ndarray, int]:
    audio, sr = sf.read(Path(path), dtype="float32", always_2d=True)
    audio = channels_first(audio.T)
    if sr != target_sr:
        from math import gcd

        factor = gcd(sr, target_sr)
        audio = resample_poly(audio, target_sr // factor, sr // factor, axis=-1).astype(np.float32)
    return audio, target_sr


def save_audio(path: str | Path, audio: np.ndarray, sr: int = SAMPLE_RATE) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, normalize(channels_first(audio)).T, sr, subtype="FLOAT", format="WAV")
    return path


def mono(audio: np.ndarray) -> np.ndarray:
    return channels_first(audio).mean(axis=0)
