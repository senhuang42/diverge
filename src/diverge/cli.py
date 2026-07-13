from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

from .config import RunConfig  # noqa: E402
from .critic import add_choice, choice_count, train_critic  # noqa: E402
from .embed import Embedder  # noqa: E402
from .generator import MockGenerator, StableAudioGenerator  # noqa: E402
from .session import run_session  # noqa: E402


def _reference(value: str) -> tuple[Path, float]:
    path, separator, weight = value.rpartition(":")
    if not separator:
        return Path(value), 1.0
    try:
        return Path(path), float(weight)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("reference must be PATH[:WEIGHT]") from exc


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="diverge")
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run")
    run.add_argument("--config", type=Path)
    run.add_argument("--source", type=Path)
    run.add_argument("--ref", action="append", type=_reference, default=[])
    run.add_argument("--transform", type=int, default=45)
    run.add_argument("--spread", type=int, default=60)
    run.add_argument("--drift", type=int, default=35)
    run.add_argument("--locks", default="groove")
    run.add_argument("--duration", type=float, default=8.0)
    run.add_argument("--seed", type=int, default=0)
    run.add_argument("--n-return", type=int, default=8)
    run.add_argument("--n-oversample", type=int)
    run.add_argument("--library-index", type=Path)
    run.add_argument("--critic-model", type=Path)
    run.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    run.add_argument("--style-hint", default="")
    run.add_argument("--output-dir", type=Path, default=Path("runs"))
    run.add_argument("--models-dir", type=Path, default=Path("models"))
    run.add_argument("--fast", action="store_true")
    run.add_argument("--batch-size", type=int)
    run.add_argument("--mock", action="store_true")

    critic = commands.add_parser("critic")
    critic_commands = critic.add_subparsers(dest="critic_command", required=True)
    add = critic_commands.add_parser("add")
    add.add_argument("wav", type=Path)
    add.add_argument("label", choices=("keep", "discard"))
    add.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    train = critic_commands.add_parser("train")
    train.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    train.add_argument("--model", type=Path, default=Path("models/critic.joblib"))
    status = critic_commands.add_parser("status")
    status.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    return parser


def _config(args: argparse.Namespace) -> RunConfig:
    if args.config:
        config = RunConfig.from_json(args.config.read_text())
        config.fast = config.fast or args.fast
        return config
    if args.source is None:
        raise SystemExit("--source is required without --config")
    return RunConfig(
        source=args.source,
        references=args.ref,
        transform=args.transform,
        spread=args.spread,
        drift=args.drift,
        locks={item.strip() for item in args.locks.split(",") if item.strip()},
        n_return=args.n_return,
        n_oversample=args.n_oversample or (16 if args.fast else 32),
        duration_s=args.duration,
        seed=args.seed,
        library_index=args.library_index,
        critic_model=args.critic_model,
        choices_path=args.choices,
        style_text_hint=args.style_hint,
        fast=args.fast,
        generation_batch_size=args.batch_size or 8,
        output_dir=args.output_dir,
    )


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "critic":
        if args.critic_command == "add":
            embedding = Embedder().embed_file(args.wav)
            add_choice(args.wav, embedding, args.label, args.choices)
            print(f"recorded {args.label}: {args.wav}")
        elif args.critic_command == "train":
            print(json.dumps(train_critic(args.choices, args.model), indent=2))
        else:
            print(json.dumps({"n": choice_count(args.choices)}))
        return 0
    config = _config(args)
    generator = (
        MockGenerator()
        if args.mock
        else StableAudioGenerator(
            args.models_dir, fast=config.fast, batch_size=config.generation_batch_size
        )
    )
    output = run_session(config, generator, Embedder())
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
