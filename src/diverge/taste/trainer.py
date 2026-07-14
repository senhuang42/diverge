from __future__ import annotations

import subprocess
import sys
import threading
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
        self._pending = False
        self._closed = False
        self._lock = threading.Lock()

    def notify(self) -> None:
        with self._lock:
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
        with self._lock:
            if self._closed or (self._process and self._process.poll() is None):
                self._pending = True
                return
            self._timer = None
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
        threading.Thread(target=self._wait, daemon=True).start()

    def _wait(self) -> None:
        process = self._process
        if process is None:
            return
        _, stderr = process.communicate()
        error = stderr.strip() if process.returncode else None
        with self._lock:
            self._process = None
            rerun = self._pending and not self._closed
            self._pending = False
        if self.on_complete:
            self.on_complete(error)
        if rerun:
            self.notify()

    def close(self) -> None:
        with self._lock:
            self._closed = True
            if self._timer:
                self._timer.cancel()
                self._timer = None
            process = self._process
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()

    def __enter__(self) -> AsyncTasteTrainer:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
