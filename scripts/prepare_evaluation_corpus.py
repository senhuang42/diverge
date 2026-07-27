#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import urllib.request
from pathlib import Path

import numpy as np

from diverge.audio_io import load_audio, save_audio


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _audio_sha256(path: Path) -> str:
    audio, sample_rate = load_audio(path)
    digest = hashlib.sha256()
    digest.update(f"{sample_rate}:{audio.shape[0]}:{audio.shape[1]}".encode())
    digest.update(np.asarray(audio, dtype="<f4").tobytes(order="C"))
    return digest.hexdigest()


def _download(url: str, target: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "Diverge-Evaluation/1"})
    with urllib.request.urlopen(request) as response:  # noqa: S310 - pinned HTTPS corpus URLs
        target.write_bytes(response.read())


def prepare(corpus_path: Path, *, refresh: bool = False) -> list[dict[str, str]]:
    corpus_path = corpus_path.resolve()
    payload = json.loads(corpus_path.read_text())
    root = corpus_path.parent
    prepared = []
    for asset_id, asset in payload["assets"].items():
        output = (root / asset["path"]).resolve()
        raw = output.parent / "raw" / f"{asset_id}.{asset['source_format']}"
        raw.parent.mkdir(parents=True, exist_ok=True)
        if refresh or not raw.is_file():
            print(f"Downloading {asset_id} from {asset['download_url']}")
            _download(asset["download_url"], raw)
        actual_source_hash = _sha256(raw)
        if actual_source_hash != asset["source_sha256"]:
            raise ValueError(
                f"source checksum mismatch for {asset_id}: "
                f"expected {asset['source_sha256']}, got {actual_source_hash}"
            )

        audio, sample_rate = load_audio(raw)
        start = round(float(asset.get("offset_s", 0)) * sample_rate)
        samples = round(float(asset["duration_s"]) * sample_rate)
        audio = audio[:, start:]
        if audio.shape[-1] < samples:
            if not asset.get("loop_source") or audio.shape[-1] == 0:
                raise ValueError(f"source {asset_id} is too short for its requested preparation")
            repeats = int(np.ceil(samples / audio.shape[-1]))
            audio = np.tile(audio, (1, repeats))
        save_audio(output, audio[:, :samples], sample_rate)
        output_hash = _audio_sha256(output)
        expected_output_hash = asset.get("prepared_sha256")
        if expected_output_hash and output_hash != expected_output_hash:
            raise ValueError(
                f"prepared checksum mismatch for {asset_id}: "
                f"expected {expected_output_hash}, got {output_hash}"
            )
        prepared.append(
            {
                "asset_id": asset_id,
                "path": str(output),
                "source_sha256": actual_source_hash,
                "prepared_sha256": output_hash,
            }
        )
    return prepared


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--corpus",
        type=Path,
        default=Path("evaluation/corpus.cc0.json"),
    )
    parser.add_argument("--refresh", action="store_true")
    args = parser.parse_args()
    print(json.dumps(prepare(args.corpus, refresh=args.refresh), indent=2))


if __name__ == "__main__":
    main()
