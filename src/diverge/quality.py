from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np


@dataclass(frozen=True)
class QualityReport:
    passed: bool
    failures: tuple[str, ...]
    channels: int
    samples: int
    expected_samples: int
    rms: float
    peak: float
    clipping_fraction: float
    max_discontinuity: float
    loop_seam_ratio: float | None

    def to_dict(self) -> dict:
        return asdict(self)


def evaluate_quality(
    audio: np.ndarray,
    expected_samples: int,
    *,
    loop: bool = False,
    silence_rms: float = 1e-4,
    clipping_fraction_limit: float = 1e-3,
    discontinuity_limit: float = 1.8,
    loop_seam_ratio_limit: float = 0.5,
) -> QualityReport:
    signal = np.asarray(audio, dtype=np.float32)
    failures: list[str] = []
    if signal.ndim == 1:
        signal = signal[np.newaxis, :]
    if signal.ndim != 2:
        return QualityReport(
            False,
            ("invalid_layout",),
            0,
            0,
            expected_samples,
            0.0,
            0.0,
            0.0,
            0.0,
            None,
        )
    channels, samples = signal.shape
    if channels not in (1, 2):
        failures.append("invalid_channel_count")
    if samples != expected_samples:
        failures.append("wrong_duration")
    if not np.isfinite(signal).all():
        failures.append("non_finite")
        signal = np.nan_to_num(signal)
    rms = float(np.sqrt(np.mean(signal**2))) if signal.size else 0.0
    peak = float(np.max(np.abs(signal))) if signal.size else 0.0
    clipping_fraction = float(np.mean(np.abs(signal) >= 0.999)) if signal.size else 0.0
    max_discontinuity = (
        float(np.max(np.abs(np.diff(signal, axis=-1)))) if samples > 1 else 0.0
    )
    if rms < silence_rms:
        failures.append("silence")
    if clipping_fraction > clipping_fraction_limit:
        failures.append("clipping")
    if max_discontinuity > discontinuity_limit:
        failures.append("severe_discontinuity")
    loop_seam_ratio = None
    if loop and samples:
        seam_delta = float(np.sqrt(np.mean((signal[:, 0] - signal[:, -1]) ** 2)))
        loop_seam_ratio = seam_delta / max(rms, 1e-12)
        if loop_seam_ratio > loop_seam_ratio_limit:
            failures.append("loop_seam")
    return QualityReport(
        not failures,
        tuple(failures),
        channels,
        samples,
        expected_samples,
        rms,
        peak,
        clipping_fraction,
        max_discontinuity,
        loop_seam_ratio,
    )
