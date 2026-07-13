import numpy as np

from diverge.select import Candidate, select_candidates


def _candidate(index: int, vector: list[float], utility: float, novelty: float = 0.5, lock=1.0):
    embedding = np.asarray(vector, dtype=np.float32)
    embedding /= np.linalg.norm(embedding)
    return Candidate(
        index, embedding, ref_fit=utility, taste=utility, novelty=novelty, lock_score=lock
    )


def test_spread_zero_returns_top_utility_cluster() -> None:
    candidates = [
        _candidate(0, [1, 0], 1.0),
        _candidate(1, [0.99, 0.1], 0.9),
        _candidate(2, [0, 1], 0.3),
    ]
    result = select_candidates(candidates, 2, spread=0, drift=0)
    assert [item.index for item in result.selected] == [0, 1]


def test_spread_high_covers_space_and_locks_relax() -> None:
    candidates = [
        _candidate(0, [1, 0], 1.0, lock=0.6),
        _candidate(1, [0.99, 0.1], 0.9, lock=0.5),
        _candidate(2, [-1, 0], 0.3, lock=0.5),
    ]
    result = select_candidates(candidates, 2, spread=100, drift=0, lock_threshold=0.55)
    assert {item.index for item in result.selected} == {0, 2}
    assert result.threshold_used == 0.5


def test_drift_monotonically_prefers_novel_candidates() -> None:
    candidates = [
        _candidate(0, [1, 0], 0.5, novelty=0.0),
        _candidate(1, [0, 1], 0.5, novelty=1.0),
    ]
    low = select_candidates(candidates, 1, spread=0, drift=0).selected[0]
    high = select_candidates(candidates, 1, spread=0, drift=100).selected[0]
    assert high.novelty >= low.novelty
