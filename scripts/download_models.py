#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path

from huggingface_hub import snapshot_download

STABLE_ID = "stabilityai/stable-audio-open-small"
CLAP_ID = "laion/clap-htsat-unfused"
T5_ID = "google-t5/t5-base"
LICENSE_URL = "https://huggingface.co/stabilityai/stable-audio-open-small"


def main() -> None:
    token = os.environ.get("HF_TOKEN")
    if not token:
        raise SystemExit(
            "HF_TOKEN is required for this one-time download. Accept the Stability Community "
            f"License first: {LICENSE_URL}"
        )
    root = Path(__file__).resolve().parents[1] / "models"
    root.mkdir(exist_ok=True)
    print(f"License/terms: {LICENSE_URL}")
    snapshot_download(CLAP_ID, token=token, local_dir=root / "clap-htsat-unfused")
    snapshot_download(
        T5_ID,
        token=token,
        local_dir=root / "t5-base",
        allow_patterns=[
            "model.safetensors",
            "config.json",
            "generation_config.json",
            "tokenizer.json",
            "tokenizer_config.json",
            "special_tokens_map.json",
            "spiece.model",
        ],
    )
    snapshot_download(
        STABLE_ID,
        token=token,
        local_dir=root / "stable-audio-open-small",
        allow_patterns=["model.safetensors", "model_config.json", "base_model_config.json"],
    )
    print(f"Models downloaded under {root}")


if __name__ == "__main__":
    main()
