from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from diverge import cli
from diverge.taste.events import CandidateRecord, TasteEvent, TasteEventStore
from diverge.taste.model import TasteModel
from diverge.taste.profile import training_events


def vector(index: int) -> np.ndarray:
    value = np.zeros(512, dtype=np.float32)
    value[index] = 1
    return value


class FakeEmbedder:
    def __init__(self, model_path: Path) -> None:
        self.model_path = model_path

    def embed_file(self, path: Path) -> np.ndarray:
        return vector(0 if path.stem == "a" else 1)

    def embed_batch(self, paths: list[Path]) -> np.ndarray:
        return np.stack([self.embed_file(path) for path in paths])


def run_cli(capsys, *arguments: object) -> dict[str, object]:
    assert cli.main([str(argument) for argument in arguments]) == 0
    return json.loads(capsys.readouterr().out)


def test_compare_records_once_trains_and_reports_profile(
    tmp_path: Path, monkeypatch, capsys
) -> None:
    monkeypatch.setattr(cli, "Embedder", FakeEmbedder)
    events = tmp_path / "taste" / "events.jsonl"
    model = tmp_path / "taste" / "model.joblib"
    common = (
        "--events",
        events,
        "--model",
        model,
        "--models-dir",
        tmp_path / "models",
        "--batch-id",
        "batch-1",
    )

    first = run_cli(capsys, "taste", "compare", "a.wav", "b.wav", "prefer_a", *common)
    duplicate = run_cli(
        capsys, "taste", "compare", "b.wav", "a.wav", "prefer_b", *common
    )
    status = run_cli(capsys, "taste", "status", "--events", events, "--model", model)

    assert first["recorded"] is True
    assert first["pairwise_observations"] == 1
    assert duplicate == {
        **duplicate,
        "recorded": False,
        "event_id": None,
        "reason": "comparison already recorded",
    }
    assert status["events"] == 1
    assert status["pairwise"] == 1
    assert status["effective_evidence"] == 1.0
    assert model.exists()


def test_learning_toggle_prevents_absolute_and_pairwise_recording(
    tmp_path: Path, monkeypatch, capsys
) -> None:
    events = tmp_path / "taste" / "events.jsonl"
    model = tmp_path / "taste" / "model.joblib"
    disabled = run_cli(capsys, "taste", "set-learning", "disabled", "--events", events)

    class UnexpectedEmbedder:
        def __init__(self, model_path: Path) -> None:
            raise AssertionError(f"embedding should not run while disabled: {model_path}")

    monkeypatch.setattr(cli, "Embedder", UnexpectedEmbedder)
    absolute = run_cli(
        capsys, "taste", "add", "a.wav", "keep", "--events", events, "--model", model
    )
    pairwise = run_cli(
        capsys,
        "taste",
        "compare",
        "a.wav",
        "b.wav",
        "prefer_a",
        "--events",
        events,
        "--model",
        model,
    )

    assert disabled["learning_enabled"] is False
    assert absolute == {"recorded": False, "reason": "learning disabled"}
    assert pairwise == {"recorded": False, "reason": "learning disabled"}
    assert len(TasteEventStore(events).load()) == 1
    assert not model.exists()


def test_reset_preserves_history_and_clears_training_state(tmp_path: Path, capsys) -> None:
    events = tmp_path / "taste" / "events.jsonl"
    model = tmp_path / "taste" / "model.joblib"
    store = TasteEventStore(events)
    store.append(
        TasteEvent(
            event_type="absolute",
            label="keep",
            candidate_a=CandidateRecord.from_embedding("a.wav", vector(0)),
        )
    )
    run_cli(capsys, "taste", "train", "--events", events, "--model", model)

    result = run_cli(capsys, "taste", "reset", "--events", events, "--model", model)
    restored = TasteModel.load(model)

    assert result["reset"] is True
    assert len(store.load()) == 2
    assert training_events(events) == []
    assert restored.observation_count == 0


def test_cli_model_export_and_import_are_validated_json_operations(
    tmp_path: Path, capsys
) -> None:
    source = tmp_path / "source.joblib"
    portable = tmp_path / "portable.joblib"
    imported = tmp_path / "imported.joblib"
    event = TasteEvent(
        event_type="absolute",
        label="love",
        candidate_a=CandidateRecord.from_embedding("a.wav", vector(0)),
    )
    model = TasteModel()
    model.fit([event])
    model.save(source)

    exported = run_cli(
        capsys, "taste", "export-model", "--model", source, "--output", portable
    )
    restored = run_cli(
        capsys, "taste", "import-model", portable, "--model", imported
    )

    assert exported == {"path": str(portable)}
    assert restored["path"] == str(imported)
    assert restored["observations"] == 1
    assert restored["positive_modes"] == 1
    assert TasteModel.load(imported).effective_count == model.effective_count
