#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from diverge.embed import Embedder
from diverge.novelty import build_index

AUDIO_SUFFIXES = {".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("--output", type=Path, default=Path("models/library_index.joblib"))
    args = parser.parse_args()
    paths = sorted(
        path for path in args.library.rglob("*") if path.suffix.lower() in AUDIO_SUFFIXES
    )
    if not paths:
        raise SystemExit("no supported audio files found")
    embeddings = Embedder().embed_batch(paths)
    build_index(embeddings, [str(path) for path in paths], args.output)
    print(f"indexed {len(paths)} files -> {args.output}")


if __name__ == "__main__":
    main()
