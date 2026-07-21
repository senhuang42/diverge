from __future__ import annotations

import subprocess
import sys
import threading
import time
from collections.abc import Callable
from pathlib import Path


class AsyncTasteTrainer:
    """Debounced single-child trainer that preserves the last valid model on failure."""

    def __init__(
        self,
        events_path: str | Path = "taste/events.jsonl",
        model_path: str | Path = "taste/model.joblib",
        *,
        debounce_seconds: float = 0.2,
        on_complete: Callable[[str | None], None] | None = None,
    ) -> None:
        self.events_path = Path(events_path)
        self.model_path = Path(model_path)
        self.debounce_seconds = debounce_seconds
        self.on_complete = on_complete
        self._timer: threading.Timer | None = None
        self._process: subprocess.Popen[str] | None = None
        self._wait_thread: threading.Thread | None = None
        self._pending = False
        self._closed = False
        self._condition = threading.Condition()

    def notify(self) -> None:
        with self._condition:
            if self._closed:
                return
            if self._process and self._process.poll() is None:
                self._pending = True
                return
            if self._timer:
                self._timer.cancel()
            self._timer = threading.Timer(self.debounce_seconds, self._start)
            self._timer.daemon = True
            self._timer.start()

    def _start(self) -> None:
        with self._condition:
            if self._closed or (self._process and self._process.poll() is None):
                if not self._closed:
                    self._pending = True
                return
            self._timer = None
            try:
                self._process = subprocess.Popen(
                    [
                        sys.executable,
                        "-m",
                        "diverge.cli",
                        "taste",
                        "train",
                        "--events",
                        str(self.events_path),
                        "--model",
                        str(self.model_path),
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            except OSError as exc:
                self._condition.notify_all()
                callback = self.on_complete
                error = str(exc)
            else:
                callback = None
                error = None
                self._wait_thread = threading.Thread(
                    target=self._wait,
                    daemon=True,
                    name="diverge-taste-trainer",
                )
                self._wait_thread.start()
        if callback:
            callback(error)

    def _wait(self) -> None:
        process = self._process
        if process is None:
            return
        _, stderr = process.communicate()
        error = stderr.strip() if process.returncode else None
        with self._condition:
            if self._process is process:
                self._process = None
            rerun = self._pending and not self._closed
            self._pending = False
            if rerun:
                self._timer = threading.Timer(self.debounce_seconds, self._start)
                self._timer.daemon = True
                self._timer.start()
            callback = self.on_complete if not self._closed else None
            self._condition.notify_all()
        if callback:
            callback(error)

    def is_active(self) -> bool:
        with self._condition:
            return bool(
                self._timer
                or self._pending
                or (self._process and self._process.poll() is None)
            )

    def wait_for_idle(self, timeout: float = 5.0) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while self._timer or self._pending or self._process is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)
            return True

    def close(self) -> None:
        with self._condition:
            if self._closed:
                return
            self._closed = True
            self._pending = False
            if self._timer:
                self._timer.cancel()
                self._timer = None
            process = self._process
            wait_thread = self._wait_thread
            self._condition.notify_all()
        if process and process.poll() is None:
            process.terminate()
        if wait_thread and wait_thread is not threading.current_thread():
            wait_thread.join(timeout=2)
        if process and process.poll() is None:
            process.kill()
            if wait_thread and wait_thread is not threading.current_thread():
                wait_thread.join(timeout=2)
        if process and process.poll() is None:
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:  # pragma: no cover - pathological OS failure
                process.kill()
                process.wait(timeout=1)

    def __enter__(self) -> AsyncTasteTrainer:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
