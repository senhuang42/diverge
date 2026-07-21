from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import joblib
import pytest

from diverge.taste.model import TasteModel
from diverge.taste.trainer import AsyncTasteTrainer


def wait_until(predicate, timeout: float = 2.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.005)
    return predicate()


def test_trainer_debounces_and_runs_one_pending_update(monkeypatch, tmp_path: Path) -> None:
    processes: list[subprocess.Popen[str]] = []
    real_popen = subprocess.Popen

    def short_process(*args, **kwargs):
        del args
        process = real_popen(
            [sys.executable, "-c", "import time; time.sleep(0.08)"],
            **kwargs,
        )
        processes.append(process)
        return process

    monkeypatch.setattr(subprocess, "Popen", short_process)
    completed: list[str | None] = []
    trainer = AsyncTasteTrainer(
        tmp_path / "events.jsonl",
        tmp_path / "model.joblib",
        debounce_seconds=0.01,
        on_complete=completed.append,
    )
    for _ in range(10):
        trainer.notify()
    assert wait_until(lambda: len(processes) == 1)
    for _ in range(10):
        trainer.notify()
    assert trainer.wait_for_idle()
    trainer.close()

    assert len(processes) == 2
    assert completed == [None, None]
    assert all(process.poll() is not None for process in processes)


def test_trainer_close_soak_reaps_every_child_and_suppresses_callbacks(
    monkeypatch, tmp_path: Path
) -> None:
    processes: list[subprocess.Popen[str]] = []
    real_popen = subprocess.Popen

    def long_process(*args, **kwargs):
        del args
        process = real_popen(
            [sys.executable, "-c", "import time; time.sleep(30)"],
            **kwargs,
        )
        processes.append(process)
        return process

    monkeypatch.setattr(subprocess, "Popen", long_process)
    callbacks: list[str | None] = []
    for iteration in range(12):
        trainer = AsyncTasteTrainer(
            tmp_path / f"events-{iteration}.jsonl",
            tmp_path / f"model-{iteration}.joblib",
            debounce_seconds=0.001,
            on_complete=callbacks.append,
        )
        trainer.notify()
        assert wait_until(lambda iteration=iteration: len(processes) > iteration)
        trainer.close()
        trainer.close()
        assert not trainer.is_active()
        assert processes[-1].poll() is not None
    assert callbacks == []


def test_atomic_model_save_failure_preserves_last_valid_snapshot(
    monkeypatch, tmp_path: Path
) -> None:
    path = tmp_path / "model.joblib"
    TasteModel().save(path)
    original = path.read_bytes()

    def fail_dump(*_args, **_kwargs) -> None:
        raise OSError("simulated interrupted write")

    monkeypatch.setattr(joblib, "dump", fail_dump)
    with pytest.raises(OSError, match="interrupted"):
        TasteModel().save(path)

    assert path.read_bytes() == original
    assert TasteModel.load(path).observation_count == 0
    assert list(tmp_path.glob(".model.joblib.*")) == []
