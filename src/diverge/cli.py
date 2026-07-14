from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

from .config import RunConfig  # noqa: E402
from .critic import add_choice, choice_count, train_critic  # noqa: E402
from .embed import Embedder  # noqa: E402
from .generator import MockGenerator, StableAudioGenerator  # noqa: E402
from .session import run_session  # noqa: E402
from .taste.events import (  # noqa: E402
    CandidateRecord,
    TasteEvent,
    TasteEventStore,
    migrate_v1,
)
from .taste.model import TasteModel  # noqa: E402
from .taste.profile import export_profile, profile_settings, training_events  # noqa: E402


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
    run.add_argument("--taste-events", type=Path, default=Path("taste/events.jsonl"))
    run.add_argument("--taste-model", type=Path, default=Path("taste/model.joblib"))
    run.add_argument("--opinion", type=int, default=50)
    run.add_argument("--disable-taste-learning", action="store_true")
    run.add_argument("--disable-prompt-enrichment", action="store_true")

    critic = commands.add_parser("critic")
    critic_commands = critic.add_subparsers(dest="critic_command", required=True)
    add = critic_commands.add_parser("add")
    add.add_argument("wav", type=Path)
    add.add_argument("label", choices=("keep", "discard"))
    add.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    add.add_argument("--models-dir", type=Path, default=Path("models"))
    train = critic_commands.add_parser("train")
    train.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    train.add_argument("--model", type=Path, default=Path("models/critic.joblib"))
    status = critic_commands.add_parser("status")
    status.add_argument("--choices", type=Path, default=Path("choices.jsonl"))

    taste = commands.add_parser("taste")
    taste_commands = taste.add_subparsers(dest="taste_command", required=True)
    taste_status = taste_commands.add_parser("status")
    taste_status.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    taste_status.add_argument("--model", type=Path, default=Path("taste/model.joblib"))
    migrate = taste_commands.add_parser("migrate")
    migrate.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    migrate.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    taste_add = taste_commands.add_parser("add")
    taste_add.add_argument("wav", type=Path)
    taste_add.add_argument("label", choices=("love", "keep", "discard", "export"))
    taste_add.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    taste_add.add_argument("--model", type=Path, default=Path("taste/model.joblib"))
    taste_add.add_argument("--models-dir", type=Path, default=Path("models"))
    taste_add.add_argument("--batch-id")
    taste_undo = taste_commands.add_parser("undo")
    taste_undo.add_argument("event_id")
    taste_undo.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    taste_undo.add_argument("--model", type=Path, default=Path("taste/model.joblib"))
    taste_train = taste_commands.add_parser("train")
    taste_train.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    taste_train.add_argument("--model", type=Path, default=Path("taste/model.joblib"))
    evaluate = taste_commands.add_parser("evaluate")
    evaluate.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    evaluate.add_argument("--reports-dir", type=Path, default=Path("taste/reports"))
    export = taste_commands.add_parser("export-profile")
    export.add_argument("--events", type=Path, default=Path("taste/events.jsonl"))
    export.add_argument("--model", type=Path, default=Path("taste/model.joblib"))
    export.add_argument("--output", type=Path, default=Path("taste/profile.json"))
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
        taste_events_path=args.taste_events,
        taste_model_path=args.taste_model,
        opinion=args.opinion,
        taste_learning_enabled=not args.disable_taste_learning,
        prompt_enrichment_enabled=not args.disable_prompt_enrichment,
    )


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "taste":
        store = TasteEventStore(args.events)
        if args.taste_command == "migrate":
            print(json.dumps(migrate_v1(args.choices, args.events), indent=2))
        elif args.taste_command == "add":
            embedding = Embedder(model_path=args.models_dir / "clap-htsat-unfused").embed_file(
                args.wav
            )
            event = store.append(
                TasteEvent(
                    event_type="export" if args.label == "export" else "absolute",
                    label=args.label,
                    candidate_a=CandidateRecord.from_embedding(args.wav, embedding),
                    batch_id=args.batch_id,
                )
            )
            model = TasteModel()
            report = model.fit(training_events(args.events))
            model.save(args.model)
            print(json.dumps({"event_id": event.event_id, **report.__dict__}, indent=2))
        elif args.taste_command == "undo":
            event = store.undo(args.event_id)
            model = TasteModel()
            report = model.fit(training_events(args.events))
            model.save(args.model)
            print(json.dumps({"event_id": event.event_id, **report.__dict__}, indent=2))
        elif args.taste_command == "status":
            events = store.load(effective=True)
            model = TasteModel()
            model_warning = None
            if args.model.exists():
                try:
                    model = TasteModel.load(args.model)
                except Exception as exc:
                    model_warning = str(exc)
            print(
                json.dumps(
                    {
                        "version": 2,
                        "events": len(events),
                        "pairwise": sum(event.event_type == "pairwise" for event in events),
                        "effective_evidence": model.effective_count,
                        "confidence": 1 - math.exp(-model.effective_count / 8),
                        "positive_modes": len(model.positive_modes),
                        "negative_modes": len(model.negative_modes),
                        "learning_enabled": profile_settings(args.events).get(
                            "learning_enabled", True
                        ),
                        "model_warning": model_warning,
                        "warnings": store.warnings,
                    },
                    indent=2,
                )
            )
        elif args.taste_command == "train":
            model = TasteModel()
            report = model.fit(training_events(args.events))
            model.save(args.model)
            print(json.dumps(report.__dict__, indent=2))
        elif args.taste_command == "evaluate":
            from .taste.evaluate import evaluate_events

            print(json.dumps(evaluate_events(store.load(), args.reports_dir), indent=2))
        else:
            print(export_profile(args.model, args.events, args.output))
        return 0
    if args.command == "critic":
        if args.critic_command == "add":
            embedding = Embedder(model_path=args.models_dir / "clap-htsat-unfused").embed_file(
                args.wav
            )
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
    try:
        output = run_session(
            config,
            generator,
            Embedder(model_path=args.models_dir / "clap-htsat-unfused"),
        )
    except Exception as exc:
        print(
            "DIVERGE_EVENT "
            + json.dumps(
                {"stage": "error", "code": type(exc).__name__, "message": str(exc)}
            ),
            flush=True,
        )
        raise
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
