from diverge.explanations import candidate_explanations


def test_explanations_use_active_preserve_and_distinct_measured_delta() -> None:
    candidates = [
        {
            "ref_fit": 0.5,
            "locks": {"groove": 0.82, "melody": 0.91},
            "descriptors": {"dark": 0.82, "percussive": 0.4, "raw": 0.3, "compressed": 0.5},
        },
        {
            "ref_fit": 0.5,
            "locks": {"groove": 0.79, "melody": 0.2},
            "descriptors": {"dark": 0.52, "percussive": 0.4, "raw": 0.3, "compressed": 0.5},
        },
    ]
    source = {"dark": 0.5, "percussive": 0.4, "raw": 0.3, "compressed": 0.5}

    explanations = candidate_explanations(candidates, source, {"groove"}, 0.55, has_direction=False)

    assert explanations[0]["text"] == "Groove retained; darker texture."
    assert explanations[0]["evidence"] == {
        "kind": "source_delta",
        "descriptor": "dark",
        "source_delta": 0.32,
        "batch_delta": 0.3,
        "preserved": "groove",
    }
    assert explanations[1] == {"text": "", "evidence": {}}


def test_explanations_reserve_direction_claim_for_a_distinct_winner() -> None:
    candidates = [
        {"ref_fit": 0.81, "locks": {"melody": 0.72}, "descriptors": {}},
        {"ref_fit": 0.69, "locks": {"melody": 0.7}, "descriptors": {}},
    ]

    explanations = candidate_explanations(candidates, {}, {"melody"}, 0.55, has_direction=True)

    assert explanations[0]["text"] == "Closest to your direction; melody retained."
    assert explanations[0]["evidence"]["kind"] == "direction_fit"
    assert explanations[1]["text"] == ""


def test_explanations_suppress_shared_or_weak_differences() -> None:
    candidates = [
        {"descriptors": {"dark": 0.7}},
        {"descriptors": {"dark": 0.68}},
    ]

    explanations = candidate_explanations(
        candidates, {"dark": 0.5}, set(), 0.55, has_direction=False
    )

    assert [item["text"] for item in explanations] == ["", ""]
