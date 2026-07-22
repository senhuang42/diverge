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


def test_preserve_threshold_is_never_relaxed_to_fill_the_set() -> None:
    candidates = [
        _candidate(0, [1, 0], 1.0, lock=0.6),
        _candidate(1, [0.99, 0.1], 0.9, lock=0.5),
        _candidate(2, [-1, 0], 0.3, lock=0.5),
    ]
    result = select_candidates(candidates, 2, spread=100, drift=0, lock_threshold=0.55)
    assert [item.index for item in result.selected] == [0]
    assert result.threshold_used == 0.55
    assert result.relaxations == []
    assert result.eligible_count == 1
    assert result.requested_count == 2


def test_preserve_threshold_can_return_an_empty_valid_subset() -> None:
    candidates = [
        _candidate(0, [1, 0], 1.0, lock=0.2),
        _candidate(1, [0, 1], 0.9, lock=0.3),
    ]

    result = select_candidates(candidates, 2, spread=100, drift=0, lock_threshold=0.55)

    assert result.selected == []
    assert result.eligible_count == 0
    assert result.requested_count == 2


def test_drift_monotonically_prefers_novel_candidates() -> None:
    candidates = [
        _candidate(0, [1, 0], 0.5, novelty=0.0),
        _candidate(1, [0, 1], 0.5, novelty=1.0),
    ]
    low = select_candidates(candidates, 1, spread=0, drift=0).selected[0]
    high = select_candidates(candidates, 1, spread=0, drift=100).selected[0]
    assert high.novelty >= low.novelty


def test_self_novelty_weight_prefers_distance_from_recent_keeps() -> None:
    repeated = _candidate(0, [1, 0], 0.5)
    repeated.self_novelty = 0.0
    fresh = _candidate(1, [0, 1], 0.5)
    fresh.self_novelty = 1.0
    result = select_candidates([repeated, fresh], 1, spread=0, drift=0, self_novelty_weight=0.05)
    assert result.selected[0].index == 1
