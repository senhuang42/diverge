from __future__ import annotations

import inspect
import json
import os
from collections.abc import Callable
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Protocol

import numpy as np
from scipy.signal import butter, sosfiltfilt

TRANSFORM_NOISE_MIN = 0.10
TRANSFORM_NOISE_MAX = 1.00


@dataclass(frozen=True)
class EngineCapabilities:
    engine_id: str
    model_id: str
    source_classes: tuple[str, ...]
    duration_s: tuple[float, float]
    sample_rate: int
    channels: tuple[int, ...]
    audio_to_audio: bool
    text_direction: bool
    audio_direction: bool
    inpainting: bool
    continuation: bool
    devices: tuple[str, ...]
    license_name: str
    license_url: str
    redistribution_review_required: bool
    latency_status: str = "unmeasured"
    protocol_version: int = 1
    upstream_revision: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def transform_to_noise(transform: int) -> float:
    return TRANSFORM_NOISE_MIN + np.clip(transform, 0, 100) / 100 * (
        TRANSFORM_NOISE_MAX - TRANSFORM_NOISE_MIN
    )


def fit_source_duration(source: np.ndarray, samples: int) -> np.ndarray:
    source_array = np.asarray(source, dtype=np.float32)
    if source_array.ndim == 1:
        source_array = np.stack([source_array, source_array])
    if source_array.shape[-1] < samples:
        repeats = int(np.ceil(samples / source_array.shape[-1]))
        source_array = np.tile(source_array, (1, repeats))
    return source_array[:, :samples].copy()


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
    capabilities = EngineCapabilities(
        engine_id="mock",
        model_id="deterministic-filtered-noise",
        source_classes=("test",),
        duration_s=(0.0, 30.0),
        sample_rate=44_100,
        channels=(1, 2),
        audio_to_audio=True,
        text_direction=False,
        audio_direction=False,
        inpainting=False,
        continuation=False,
        devices=("cpu",),
        license_name="repository test fixture",
        license_url="",
        redistribution_review_required=False,
        latency_status="test-only",
    )

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
        source = fit_source_duration(source, samples)
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
    capabilities = EngineCapabilities(
        engine_id="stable-audio-open-small",
        model_id=MODEL_ID,
        source_classes=("music", "sound-effects"),
        duration_s=(0.25, 47.0),
        sample_rate=44_100,
        channels=(1, 2),
        audio_to_audio=True,
        text_direction=True,
        audio_direction=False,
        inpainting=False,
        continuation=False,
        devices=("cpu", "mps"),
        license_name="Stability AI Community License",
        license_url="https://stability.ai/license",
        redistribution_review_required=True,
    )

    def __init__(
        self,
        models_dir: str | Path = "models",
        *,
        fast: bool = False,
        batch_size: int = 8,
    ) -> None:
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
        self.models_dir = Path(models_dir)
        self.fast = fast
        self.batch_size = max(1, batch_size)
        self._model = None
        self._config = None
        self.progress = print
        self.inference_settings = {
            "steps": 4 if fast else 8,
            "cfg_scale": 1.0,
            "sampler_type": "pingpong",
            "batch_size": self.batch_size,
        }

    @staticmethod
    def _generate_seed_batch(
        model,
        conditioning_tensors: dict,
        init,
        init_sr: int,
        sample_size: int,
        seeds: list[int],
        noise_level: float,
        settings: dict,
    ):
        """Pinned stable-audio-tools sampler path with deterministic per-item seeds."""
        import torch
        from stable_audio_tools.inference.generation import prepare_audio, sample_rf

        device = next(model.parameters()).device
        latent_size = sample_size
        if model.pretransform is not None:
            latent_size //= model.pretransform.downsampling_ratio
        noise_parts = []
        for seed in seeds:
            torch.manual_seed(seed)
            noise_parts.append(torch.randn([1, model.io_channels, latent_size], device=device))
        noise = torch.cat(noise_parts)
        conditioning_inputs = model.get_conditioning_inputs(conditioning_tensors)
        prepared = prepare_audio(
            init,
            in_sr=init_sr,
            target_sr=model.sample_rate,
            target_length=sample_size,
            target_channels=(
                model.pretransform.io_channels
                if model.pretransform is not None
                else model.io_channels
            ),
            device=device,
        )
        if model.pretransform is not None:
            prepared = model.pretransform.encode(prepared)
        prepared = prepared.repeat(len(seeds), 1, 1)
        model_dtype = next(model.model.parameters()).dtype
        noise = noise.to(model_dtype)
        prepared = prepared.to(model_dtype)
        conditioning_inputs = {
            key: value.to(model_dtype) if value is not None else value
            for key, value in conditioning_inputs.items()
        }
        sampled = sample_rf(
            model.model,
            noise,
            init_data=prepared,
            steps=settings["steps"],
            sampler_type=settings["sampler_type"],
            sigma_max=noise_level,
            dist_shift=model.dist_shift,
            cfg_scale=settings["cfg_scale"],
            batch_cfg=True,
            rescale_cfg=True,
            device=device,
            **conditioning_inputs,
        )
        if model.pretransform is not None:
            sampled = sampled.to(next(model.pretransform.parameters()).dtype)
            sampled = torch.cat(
                [model.pretransform.decode(chunk) for chunk in sampled.split(2)], dim=0
            )
        return sampled

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
        source_array = fit_source_duration(source, sample_size)
        init = torch.from_numpy(source_array.copy()).to(device=device, dtype=torch.float32)
        params = inspect.signature(generate_diffusion_cond).parameters
        if "init_audio" not in params or "init_noise_level" not in params:
            raise RuntimeError(
                "stable-audio-tools does not expose the required init-audio SDEdit API"
            )
        outputs: list[np.ndarray] = []
        index = 0
        active_batch_size = min(self.batch_size, n)
        while index < n:
            size = min(active_batch_size, n - index)
            seeds = list(range(seed + index, seed + index + size))
            conditioning = [
                {"prompt": style_text_hint, "seconds_start": 0, "seconds_total": duration_s}
                for _ in seeds
            ]
            try:
                with torch.inference_mode():
                    conditioning_tensors = model.conditioner(conditioning, device)
                    audio = self._generate_seed_batch(
                        model,
                        conditioning_tensors,
                        init,
                        sr,
                        sample_size,
                        seeds,
                        float(transform_to_noise(transform)),
                        self.inference_settings,
                    )
            except RuntimeError as exc:
                if size == 1 or "out of memory" not in str(exc).lower():
                    raise
                active_batch_size = max(1, size // 2)
                if hasattr(torch, "mps"):
                    torch.mps.empty_cache()
                self.progress(f"BATCH_RETRY {size}->{active_batch_size}")
                continue
            arrays = audio.detach().float().cpu().numpy()
            outputs.extend(arrays)
            index += size
            for completed in range(index - size + 1, index + 1):
                self.progress(f"PROGRESS {completed}/{n}")
        return outputs


class StableAudio3Generator:
    """Evaluation adapter for the official Stable Audio 3 Python API.

    The upstream package currently has dependency requirements that conflict with Diverge's
    production environment. This adapter therefore keeps the import optional and is intended for
    the Phase 0 evaluation environment until the backend is packaged as a separate helper.
    """

    MODEL_IDS = {
        "small-music": "stabilityai/stable-audio-3-small-music",
        "small-sfx": "stabilityai/stable-audio-3-small-sfx",
    }
    emits_progress = True

    def __init__(
        self,
        model_name: str,
        models_dir: str | Path = "models/sa3",
        *,
        device: str | None = None,
        batch_size: int = 8,
        model_factory: Callable[..., Any] | None = None,
    ) -> None:
        if model_name not in self.MODEL_IDS:
            raise ValueError(f"unsupported Stable Audio 3 model: {model_name}")
        self.model_name = model_name
        self.models_dir = Path(models_dir)
        self.device = device
        self.batch_size = max(1, batch_size)
        self._model_factory = model_factory
        self._model = None
        self.progress = print
        source_class = "music" if model_name == "small-music" else "sound-effects"
        self.capabilities = EngineCapabilities(
            engine_id=f"stable-audio-3-{model_name}",
            model_id=self.MODEL_IDS[model_name],
            source_classes=(source_class,),
            duration_s=(0.25, 120.0),
            sample_rate=44_100,
            channels=(1, 2),
            audio_to_audio=True,
            text_direction=True,
            audio_direction=False,
            inpainting=True,
            continuation=True,
            devices=("cpu", "mps"),
            license_name="Stability AI Community License",
            license_url="https://stability.ai/license",
            redistribution_review_required=True,
            upstream_revision="124e8a799f57a1f665495ecb72e547d0a62867f1",
        )
        self.inference_settings = {
            "steps": 8,
            "cfg_scale": 1.0,
            "batch_size": self.batch_size,
            "model_name": model_name,
            "device": device or "auto",
        }

    def _load(self):
        if self._model is not None:
            return self._model
        self.models_dir.mkdir(parents=True, exist_ok=True)
        os.environ.setdefault("HF_HOME", str(self.models_dir.resolve()))
        factory = self._model_factory
        if factory is None:
            try:
                from stable_audio_3 import StableAudioModel
            except ImportError as exc:
                raise RuntimeError(
                    "Stable Audio 3 is not installed in this evaluation environment; "
                    "see the Phase 0 setup in README.md"
                ) from exc
            factory = StableAudioModel.from_pretrained
        self._model = factory(self.model_name, device=self.device)
        return self._model

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
        minimum, maximum = self.capabilities.duration_s
        if not minimum <= duration_s <= maximum:
            raise ValueError(
                f"{self.model_name} duration must be between {minimum} and {maximum} seconds"
            )
        import torch

        model = self._load()
        samples = max(1, round(duration_s * sr))
        init = torch.from_numpy(fit_source_duration(source, samples))
        outputs: list[np.ndarray] = []
        index = 0
        while index < n:
            size = min(self.batch_size, n - index)
            audio = model.generate(
                prompt=style_text_hint,
                duration=duration_s,
                steps=self.inference_settings["steps"],
                cfg_scale=self.inference_settings["cfg_scale"],
                batch_size=size,
                seed=seed + index,
                init_audio=(sr, init),
                init_noise_level=float(transform_to_noise(transform)),
                truncate_output_to_duration=True,
            )
            arrays = audio.detach().float().cpu().numpy()
            if arrays.ndim != 3 or len(arrays) != size:
                raise RuntimeError(
                    f"Stable Audio 3 returned shape {arrays.shape}; "
                    f"expected ({size}, channels, samples)"
                )
            outputs.extend(array.astype(np.float32, copy=False) for array in arrays)
            index += size
            for completed in range(index - size + 1, index + 1):
                self.progress(f"PROGRESS {completed}/{n}")
        return outputs
