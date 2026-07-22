from pathlib import Path

import numpy as np

from diverge.audio_io import load_audio
from diverge.fallback import lock_safe_variations
from diverge.features import groove_similarity, melody_similarity
from diverge.quality import evaluate_quality

DATA = Path(__file__).parents[1] / "data"


def test_lock_safe_bank_contains_eight_guaranteed_source_equivalents() -> None:
    source, sr = load_audio(DATA / "loop_a.wav")

    variations = lock_safe_variations(source, sr, source.shape[-1], count=12)

    assert len(variations) == 12
    assert sum(item.source_equivalent_embedding for item in variations) == 8
    assert len({item.treatment for item in variations}) == 12
    for item in variations:
        assert evaluate_quality(item.audio, source.shape[-1]).passed
    for item in variations[-8:]:
        assert groove_similarity(item.audio, source, sr) > 0.99
        assert melody_similarity(item.audio, source, sr) > 0.99
        assert not np.array_equal(item.audio, source)
