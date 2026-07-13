from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

LOCKS = {"groove", "melody", "timbre"}


@dataclass
class RunConfig:
    source: Path
    references: list[tuple[Path, float]]
    transform: int = 45
    spread: int = 60
    drift: int = 35
    locks: set[str] = field(default_factory=lambda: {"groove"})
    n_return: int = 8
    n_oversample: int = 32
    duration_s: float = 8.0
    seed: int = 0
    library_index: Path | None = None
    critic_model: Path | None = None
    style_text_hint: str = ""
    lock_threshold: float = 0.55
    fast: bool = False
    output_dir: Path = Path("runs")

    def __post_init__(self) -> None:
        self.source = Path(self.source)
        self.references = [(Path(path), float(weight)) for path, weight in self.references]
        self.locks = set(self.locks)
        for name, value in (
            ("transform", self.transform),
            ("spread", self.spread),
            ("drift", self.drift),
        ):
            if not 0 <= value <= 100:
                raise ValueError(f"{name} must be in 0..100")
        if not self.locks <= LOCKS:
            raise ValueError(f"unknown locks: {sorted(self.locks - LOCKS)}")
        if self.n_return < 1 or self.n_oversample < self.n_return:
            raise ValueError("n_oversample must be >= n_return >= 1")
        if not 0 < self.duration_s <= 30:
            raise ValueError("duration_s must be in (0, 30]")
        if not 0 <= self.lock_threshold <= 1:
            raise ValueError("lock_threshold must be in 0..1")
        if self.references:
            if any(not 0 <= weight <= 1 for _, weight in self.references):
                raise ValueError("reference weights must be in 0..1")
            total = sum(weight for _, weight in self.references)
            if total <= 0:
                raise ValueError("at least one reference weight must be positive")
            normalized = [(path, round(weight / total, 12)) for path, weight in self.references]
            if len(normalized) > 1:
                normalized[-1] = (
                    normalized[-1][0],
                    round(1.0 - sum(weight for _, weight in normalized[:-1]), 12),
                )
            self.references = normalized
        self.library_index = Path(self.library_index) if self.library_index else None
        self.critic_model = Path(self.critic_model) if self.critic_model else None
        self.output_dir = Path(self.output_dir)

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["source"] = str(self.source)
        result["references"] = [[str(p), w] for p, w in self.references]
        result["locks"] = sorted(self.locks)
        for key in ("library_index", "critic_model", "output_dir"):
            result[key] = str(result[key]) if result[key] is not None else None
        return result

    def to_json(self, *, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> RunConfig:
        return cls(**value)

    @classmethod
    def from_json(cls, value: str) -> RunConfig:
        return cls.from_dict(json.loads(value))
