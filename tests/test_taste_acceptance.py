from __future__ import annotations

from diverge.taste.acceptance import run_acceptance_simulations


def test_deterministic_taste_acceptance_simulations() -> None:
    report = run_acceptance_simulations()

    assert all(result["decisions"] == 4 for result in report["few_shot"].values())
    assert all(result["lift_over_neutral"] >= 0.3 for result in report["few_shot"].values())
    assert report["comparison_profiles_at_or_above_0_65_auc"] >= 4
    assert all(
        result["comparisons"] == 6
        for result in report["six_targeted_comparisons"].values()
    )
    assert report["mean_favorite_opinion_lift_vs_v1"] >= 0.05
    assert report["informative_comparison"]["reduction"] > 0
    assert report["selection"]["lock_violations"] == 0
    assert report["selection"]["mean_pairwise_distance"] > 0.25
    assert report["selection"]["positive_modes_selected"] >= 2
    assert report["selection"]["roles_selected"] == 4
    assert report["selection"]["high_opinion_lift"] >= 0.05
