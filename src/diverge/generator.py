from __future__ import annotations

import inspect
import json
import os
from pathlib import Path
from typing import Protocol

import numpy as np
from scipy.signal import butter, sosfiltfilt

TRANSFORM_NOISE_MIN = 0.10
TRANSFORM_NOISE_MAX = 1.00


def transform_to_noise(transform: int) -> float:
    return TRANSFORM_NOISE_MIN + np.clip(transform, 0, 100) / 100 * (
        TRANSFORM_NOISE_MAX - TRANSFORM_NOISE_MIN
    )


class GeneratorProtocol(Protocol):
    def generate(
        self,
        source: np.ndarray,
        sr: int,
        style_embedding: np.ndarray,
        style_text_hint: str,
        transform: int,
        duration_s: float,
        seed: int,
        n: int,
    ) -> list[np.ndarray]: ...


class MockGenerator:
    """Deterministic, musical-ish filtered-noise variants for weight-free tests."""

    emits_progress = False

    def generate(
        self,
        source: np.ndarray,
        sr: int,
        style_embedding: np.ndarray,
        style_text_hint: str,
        transform: int,
        duration_s: float,
        seed: int,
        n: int,
    ) -> list[np.ndarray]:
        del style_embedding, style_text_hint
        samples = max(1, round(duration_s * sr))
        source = np.asarray(source, dtype=np.float32)
        if source.ndim == 1:
            source = np.stack([source, source])
        source = np.resize(source, (2, samples))
        mix = np.clip(transform / 100, 0.0, 1.0)
        output = []
        for offset in range(n):
            rng = np.random.default_rng(seed + offset)
            center = 120 + ((seed + offset) * 311 % 7_000)
            low = max(30, center / 2) / (sr / 2)
            high = min(sr / 2 - 100, center * 2) / (sr / 2)
            noise = rng.normal(0, 0.2, source.shape).astype(np.float32)
            sos = butter(3, [low, max(low + 0.001, high)], btype="bandpass", output="sos")
            colored = sosfiltfilt(sos, noise, axis=-1).astype(np.float32)
            envelope = 0.65 + 0.35 * np.sin(
                np.linspace(0, np.pi * (2 + offset % 5), samples, dtype=np.float32)
            )
            variant = (1 - 0.75 * mix) * source + (0.15 + 0.75 * mix) * colored * envelope
            peak = max(float(np.max(np.abs(variant))), 1e-8)
            output.append((variant * min(1.0, 0.95 / peak)).astype(np.float32))
        return output


class StableAudioGenerator:
    MODEL_ID = "stabilityai/stable-audio-open-small"
    emits_progress = True

    def __init__(self, models_dir: str | Path = "models", *, fast: bool = False) -> None:
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        self.models_dir = Path(models_dir)
        self.fast = fast
        self._model = None
        self._config = None
        self.progress = print

    def _load(self):
        if self._model is not None:
            return self._model, self._config
        model_dir = self.models_dir / "stable-audio-open-small"
        checkpoint = model_dir / "model.safetensors"
        if not checkpoint.exists():
            checkpoint = model_dir / "model.ckpt"
        if not checkpoint.exists():
            checkpoint = model_dir / "base_model.ckpt"
        config_path = model_dir / "model_config.json"
        if not checkpoint.exists() or not config_path.exists():
            raise FileNotFoundError(
                f"Missing checkpoint/config under {model_dir}. "
                "Run scripts/download_models.py with HF_TOKEN set."
            )
        try:
            import torch
            from stable_audio_tools.models.conditioners import T5Conditioner
            from stable_audio_tools.models.factory import create_model_from_config
            from stable_audio_tools.models.utils import load_ckpt_state_dict
        except ImportError as exc:
            raise RuntimeError("Install the real generator with: uv sync --extra real") from exc
        device = "mps" if torch.backends.mps.is_available() else "cpu"
        config = json.loads(config_path.read_text())
        t5_dir = self.models_dir / "t5-base"
        if not t5_dir.exists():
            raise FileNotFoundError(
                f"Missing {t5_dir}. Re-run scripts/download_models.py to install T5 locally."
            )
        for conditioner in config.get("model", {}).get("conditioning", {}).get("configs", []):
            if conditioner.get("type") == "t5":
                local_t5 = str(t5_dir.resolve())
                conditioner["config"]["t5_model_name"] = local_t5
                if local_t5 not in T5Conditioner.T5_MODELS:
                    T5Conditioner.T5_MODELS.append(local_t5)
                    T5Conditioner.T5_MODEL_DIMS[local_t5] = 768
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")
        model = create_model_from_config(config)
        model.load_state_dict(load_ckpt_state_dict(str(checkpoint)))
        model = model.to(device).float().eval()
        self._model, self._config = model, config
        return model, config

    def generate(
        self,
        source: np.ndarray,
        sr: int,
        style_embedding: np.ndarray,
        style_text_hint: str,
        transform: int,
        duration_s: float,
        seed: int,
        n: int,
    ) -> list[np.ndarray]:
        del style_embedding
        import torch
        from stable_audio_tools.inference.generation import generate_diffusion_cond

        model, config = self._load()
        device = next(model.parameters()).device
        target_sr = int(config["sample_rate"])
        sample_size = min(int(config["sample_size"]), round(duration_s * target_sr))
        init = torch.from_numpy(np.asarray(source)).to(device=device, dtype=torch.float32)
        params = inspect.signature(generate_diffusion_cond).parameters
        if "init_audio" not in params or "init_noise_level" not in params:
            raise RuntimeError(
                "stable-audio-tools 0.0.19 lacks the required SDEdit API; see NOTES.md"
            )
        conditioning = [
            {"prompt": style_text_hint, "seconds_start": 0, "seconds_total": duration_s}
        ]
        with torch.inference_mode():
            conditioning_tensors = model.conditioner(conditioning, device)
        outputs: list[np.ndarray] = []
        for index in range(n):
            kwargs = dict(
                model=model,
                steps=8 if self.fast else 50,
                cfg_scale=6.0,
                conditioning_tensors=conditioning_tensors,
                sample_size=sample_size,
                seed=seed + index,
                device=device,
                init_audio=(sr, init),
                init_noise_level=float(transform_to_noise(transform)),
            )
            audio = generate_diffusion_cond(**kwargs)
            array = audio.detach().float().cpu().numpy()
            outputs.append(np.squeeze(array, axis=0) if array.ndim == 3 else array)
            self.progress(f"PROGRESS {index + 1}/{n}")
        return outputs
