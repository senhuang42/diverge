#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path

from huggingface_hub import snapshot_download

STABLE_ID = "stabilityai/stable-audio-open-small"
CLAP_ID = "laion/clap-htsat-unfused"
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
        STABLE_ID,
        token=token,
        local_dir=root / "stable-audio-open-small",
        allow_patterns=["*.ckpt", "*.json", "*.txt", "*.model", "*.safetensors"],
    )
    print(f"Models downloaded under {root}")


if __name__ == "__main__":
    main()
