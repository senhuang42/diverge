from __future__ import annotations

import uuid
from collections.abc import Callable

import numpy as np

from .events import CandidateRecord, TasteEvent

PROFILE_NAMES = (
    "low_band",
    "dry_percussive",
    "two_mode",
    "contextual_reversal",
    "noisy",
    "drift",
)


def synthetic_utility(name: str, value: np.ndarray, category: str, time: int) -> float:
    low, dry, percussive, mode_b = value[:4]
    if name == "low_band":
        return float(low)
    if name == "dry_percussive":
        return float(0.5 * dry + 0.5 * percussive)
    if name == "two_mode":
        return float(max(low, mode_b))
    if name == "contextual_reversal":
        return float(low if category == "drum" else 1 - low)
    if name == "drift":
        return float(low if time < 12 else 1 - low)
    return float(0.5 * low + 0.25 * dry + 0.25 * percussive)


def synthetic_events(
    profile: str,
    *,
    decisions: int = 24,
    seed: int = 0,
) -> list[TasteEvent]:
    """Deterministic chronological batches for all required taste behaviors."""
    if profile not in PROFILE_NAMES:
        raise ValueError(f"unknown synthetic taste profile: {profile}")
    rng = np.random.default_rng(seed)
    output: list[TasteEvent] = []
    for index in range(decisions):
        category = "drum" if index % 2 == 0 else "melodic"
        vector = rng.normal(0, 0.02, 512).astype(np.float32)
        vector[:4] = rng.uniform(0, 1, 4)
        vector /= max(float(np.linalg.norm(vector)), 1e-12)
        preference = synthetic_utility(profile, vector, category, index)
        if profile == "noisy" and rng.random() < 0.35:
            preference = 1 - preference
        label = "keep" if preference >= 0.5 else "discard"
        candidate = CandidateRecord.from_embedding(
            f"synthetic/{profile}/{index}.wav",
            vector,
            {
                "source_category": category,
                "source_embedding": (
                    np.eye(1, 512, 510 if category == "drum" else 511, dtype=np.float32)
                    .reshape(-1)
                    .tolist()
                ),
            },
        )
        output.append(
            TasteEvent(
                event_type="absolute",
                label=label,
                candidate_a=candidate,
                batch_id=f"{profile}-batch-{index // 4:02d}",
                event_id=str(
                    uuid.uuid5(uuid.NAMESPACE_URL, f"diverge:synthetic:{profile}:{seed}:{index}")
                ),
                ts=f"2026-01-{index // 24 + 1:02d}T{index % 24:02d}:00:00+00:00",
            )
        )
    return output


def all_profiles(
    factory: Callable[..., list[TasteEvent]] = synthetic_events,
) -> dict[str, list[TasteEvent]]:
    return {name: factory(name, seed=index) for index, name in enumerate(PROFILE_NAMES)}
