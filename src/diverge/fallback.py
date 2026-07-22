from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.signal import butter, sosfiltfilt

from .audio_io import channels_first


@dataclass(frozen=True)
class FallbackVariation:
    audio: np.ndarray
    treatment: str
    source_equivalent_embedding: bool = False


def _fit_and_normalize(source: np.ndarray, samples: int) -> np.ndarray:
    audio = np.nan_to_num(channels_first(source)).astype(np.float32, copy=True)
    if audio.shape[-1] < samples:
        repeats = int(np.ceil(samples / max(1, audio.shape[-1])))
        audio = np.tile(audio, (1, repeats))
    audio = audio[:, :samples]
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak > 0.82:
        audio *= 0.82 / peak
    return audio


def _filter(audio: np.ndarray, sr: int, kind: str, cutoff: float) -> np.ndarray:
    sos = butter(2, cutoff / (sr / 2), btype=kind, output="sos")
    return sosfiltfilt(sos, audio, axis=-1).astype(np.float32)


def lock_safe_variations(
    source: np.ndarray,
    sr: int,
    samples: int,
    *,
    count: int = 16,
    guaranteed_count: int = 8,
) -> list[FallbackVariation]:
    """Create bounded source-derived options before resorting to transparent gain variants.

    These candidates still pass through the normal quality and optional similarity gates. The
    final gain variants deliberately keep the source structure intact, making the requested result
    count deterministic even when the generative pool misses every active compatibility constraint.
    """
    base = _fit_and_normalize(source, samples)
    if not base.size or float(np.sqrt(np.mean(base**2))) < 1e-4:
        return []

    low_9k = _filter(base, sr, "lowpass", min(9_000.0, sr * 0.42))
    low_3k = _filter(base, sr, "lowpass", min(3_000.0, sr * 0.30))
    low_450 = _filter(base, sr, "lowpass", min(450.0, sr * 0.12))
    high_70 = _filter(base, sr, "highpass", 70.0)
    saturated = np.tanh(base * 1.35).astype(np.float32) / np.float32(np.tanh(1.35))
    presence = (base + 0.08 * (base - low_3k)).astype(np.float32)
    warmth = (base + 0.10 * low_450).astype(np.float32)
    echo = base.copy()
    delay = max(1, round(0.012 * sr))
    if delay < samples:
        echo[:, delay:] += 0.08 * base[:, :-delay]

    treatments: list[tuple[np.ndarray, str]] = [
        (low_9k, "gentle low-pass"),
        (high_70, "sub cleanup"),
        (saturated, "soft saturation"),
        (presence, "presence lift"),
        (warmth, "low warmth"),
        (echo, "short ambience"),
    ]
    if base.shape[0] == 2:
        mid = (base[0] + base[1]) * 0.5
        side = (base[0] - base[1]) * 0.5
        treatments.extend(
            [
                (np.stack([mid + 0.72 * side, mid - 0.72 * side]), "focused stereo"),
                (np.stack([mid + 1.18 * side, mid - 1.18 * side]), "wider stereo"),
            ]
        )

    output: list[FallbackVariation] = []
    creative_count = min(len(treatments), max(0, count - guaranteed_count), 4)
    for audio, treatment in treatments[:creative_count]:
        peak = float(np.max(np.abs(audio))) if audio.size else 0.0
        if peak > 0.98:
            audio = audio * np.float32(0.98 / peak)
        output.append(FallbackVariation(audio.astype(np.float32, copy=False), treatment))
    for index, gain in enumerate(np.linspace(0.70, 0.98, max(count, 8))):
        if len(output) >= count:
            break
        output.append(
            FallbackVariation(
                (base * np.float32(gain)).astype(np.float32, copy=False),
                f"level contour {index + 1}",
                source_equivalent_embedding=True,
            )
        )
    return output
