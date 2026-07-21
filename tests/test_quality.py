import numpy as np

from diverge.quality import evaluate_quality


def test_quality_gate_accepts_exact_clean_stereo() -> None:
    phase = np.linspace(0, 2 * np.pi, 4_410, endpoint=False)
    audio = np.stack([0.2 * np.sin(phase), 0.2 * np.sin(phase)])
    report = evaluate_quality(audio, 4_410, loop=True)
    assert report.passed
    assert report.failures == ()
    assert report.loop_seam_ratio is not None


def test_quality_gate_reports_independent_failures() -> None:
    audio = np.zeros((3, 10), dtype=np.float32)
    audio[0, -1] = 2
    report = evaluate_quality(audio, 12, loop=True)
    assert not report.passed
    assert set(report.failures) >= {
        "invalid_channel_count",
        "wrong_duration",
        "clipping",
        "severe_discontinuity",
        "loop_seam",
    }
