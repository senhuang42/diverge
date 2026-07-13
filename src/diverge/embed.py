from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Protocol

import numpy as np
from scipy.signal import resample_poly

from .audio_io import load_audio, mono

MODEL_ID = "laion/clap-htsat-unfused"
DEFAULT_CACHE_DIR = Path.home() / ".cache/diverge"


class EmbeddingBackend(Protocol):
    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray: ...


class TransformersClapBackend:
    def __init__(self, model_id: str = MODEL_ID, *, local_files_only: bool = True) -> None:
        import torch
        from transformers import AutoProcessor, ClapAudioModelWithProjection

        self.torch = torch
        self.processor = AutoProcessor.from_pretrained(model_id, local_files_only=local_files_only)
        self.model = ClapAudioModelWithProjection.from_pretrained(
            model_id, local_files_only=local_files_only
        ).eval()
        self.device = torch.device("mps" if torch.backends.mps.is_available() else "cpu")
        self.model.to(self.device)

    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray:
        model_sample_rate = 48_000
        if sample_rate != model_sample_rate:
            audio = [
                resample_poly(signal, model_sample_rate, sample_rate).astype(np.float32)
                for signal in audio
            ]
        inputs = self.processor(
            audios=audio, sampling_rate=model_sample_rate, return_tensors="pt", padding=True
        )
        inputs = {key: value.to(self.device) for key, value in inputs.items()}
        with self.torch.inference_mode():
            output = self.model(**inputs).audio_embeds
        return output.float().cpu().numpy()


def _normalize(rows: np.ndarray) -> np.ndarray:
    rows = np.atleast_2d(np.asarray(rows, dtype=np.float32))
    norms = np.linalg.norm(rows, axis=1, keepdims=True)
    return rows / np.maximum(norms, 1e-12)


class Embedder:
    def __init__(
        self,
        model_id: str = MODEL_ID,
        cache_dir: str | Path = DEFAULT_CACHE_DIR,
        backend: EmbeddingBackend | None = None,
        *,
        allow_download: bool = False,
        model_path: str | Path | None = None,
    ) -> None:
        self.model_id = model_id
        self.cache_dir = Path(cache_dir)
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self._backend = backend
        self.allow_download = allow_download
        bundled = Path("models/clap-htsat-unfused")
        self.model_path = (
            Path(model_path) if model_path else (bundled if bundled.exists() else None)
        )

    @property
    def backend(self) -> EmbeddingBackend:
        if self._backend is None:
            source = str(self.model_path) if self.model_path else self.model_id
            self._backend = TransformersClapBackend(
                source, local_files_only=not self.allow_download
            )
        return self._backend

    def _key(self, path: Path) -> str:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        return hashlib.sha256(f"{digest}:{self.model_id}".encode()).hexdigest()

    def embed_file(self, path: str | Path) -> np.ndarray:
        return self.embed_batch([path])[0]

    def embed_batch(self, paths: list[str | Path]) -> np.ndarray:
        paths = [Path(path) for path in paths]
        if not paths:
            return np.empty((0, 512), dtype=np.float32)
        result: list[np.ndarray | None] = [None] * len(paths)
        missing_audio: list[np.ndarray] = []
        missing_indexes: list[int] = []
        keys: list[str] = []
        for index, path in enumerate(paths):
            key = self._key(path)
            keys.append(key)
            cache_path = self.cache_dir / f"{key}.npy"
            metadata_path = self.cache_dir / f"{key}.json"
            if cache_path.exists() and metadata_path.exists():
                result[index] = np.load(cache_path).astype(np.float32)
            else:
                audio, _ = load_audio(path)
                missing_audio.append(mono(audio))
                missing_indexes.append(index)
        if missing_audio:
            embedded = _normalize(self.backend.embed(missing_audio, 44_100))
            if embedded.shape[1] != 512:
                raise ValueError(
                    f"CLAP backend returned {embedded.shape[1]} dimensions, expected 512"
                )
            for index, vector in zip(missing_indexes, embedded, strict=True):
                np.save(self.cache_dir / f"{keys[index]}.npy", vector)
                (self.cache_dir / f"{keys[index]}.json").write_text(
                    json.dumps({"model_id": self.model_id, "source": str(paths[index])})
                )
                result[index] = vector
        return _normalize(np.stack([value for value in result if value is not None]))
