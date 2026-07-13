from pathlib import Path

import librosa
import numpy as np

from diverge.audio_io import load_audio
from diverge.features import groove_similarity, melody_similarity, timbre_similarity

DATA = Path(__file__).parents[1] / "data"


def test_identical_clip_scores_one() -> None:
    audio, sr = load_audio(DATA / "loop_a.wav")
    assert groove_similarity(audio, audio, sr) > 0.99
    assert melody_similarity(audio, audio, sr) > 0.99
    vector = np.arange(1, 513, dtype=np.float32)
    assert timbre_similarity(vector, vector) > 0.99


def test_noise_has_lower_melodic_similarity() -> None:
    audio, sr = load_audio(DATA / "loop_a.wav")
    noise, _ = load_audio(DATA / "noise.wav")
    assert melody_similarity(audio, noise, sr) < melody_similarity(audio, audio, sr) - 0.2


def test_pitch_shift_preserves_groove_but_changes_melody() -> None:
    audio, sr = load_audio(DATA / "loop_a.wav")
    shifted = np.stack(
        [librosa.effects.pitch_shift(channel, sr=sr, n_steps=5) for channel in audio]
    )
    assert groove_similarity(audio, shifted, sr) > 0.65
    assert melody_similarity(audio, shifted, sr) < 0.8


def test_lowpass_preserves_structure() -> None:
    audio, sr = load_audio(DATA / "loop_a.wav")
    lowpass, _ = load_audio(DATA / "loop_a_lowpass.wav")
    assert groove_similarity(audio, lowpass, sr) > 0.7
    assert melody_similarity(audio, lowpass, sr) > 0.7
