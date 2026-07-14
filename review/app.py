from __future__ import annotations

import argparse
import html
import json
from collections.abc import Callable
from dataclasses import dataclass
from functools import partial
from pathlib import Path

from diverge.critic import add_choice, train_critic
from diverge.embed import Embedder
from diverge.taste.events import CandidateRecord, TasteEvent, TasteEventStore
from diverge.taste.model import TasteModel, load_or_neutral
from diverge.taste.profile import (
    edit_profile,
    export_model,
    export_profile,
    import_model,
    infer_descriptors,
    profile_dict,
    profile_settings,
    reset_profile,
    training_events,
)


@dataclass(frozen=True)
class ReviewBundle:
    run_dir: Path
    manifest: dict
    map_points: list[dict]

    @property
    def candidates(self) -> list[dict]:
        return self.manifest["candidates"]


def load_bundle(run_dir: str | Path) -> ReviewBundle:
    run_dir = Path(run_dir).resolve()
    manifest_path = run_dir / "manifest.json"
    map_path = run_dir / "map.json"
    if not manifest_path.exists() or not map_path.exists():
        raise FileNotFoundError(f"{run_dir} is not a Diverge run bundle")
    manifest = json.loads(manifest_path.read_text())
    points = json.loads(map_path.read_text())
    if not manifest.get("candidates"):
        raise ValueError("run bundle has no candidates")
    return ReviewBundle(run_dir, manifest, points)


def render_map_html(points: list[dict], width: int = 720, height: int = 420) -> str:
    if not points:
        return "<p>No map data.</p>"
    xs = [float(point["x"]) for point in points]
    ys = [float(point["y"]) for point in points]
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)

    def scale(value: float, low: float, high: float, size: int) -> float:
        return 30 + (value - low) / max(high - low, 1e-9) * (size - 60)

    shapes = []
    for point in points:
        x = scale(float(point["x"]), x_min, x_max, width)
        y = height - scale(float(point["y"]), y_min, y_max, height)
        kind = point["kind"]
        title = html.escape(Path(point.get("path", kind)).name)
        if kind == "source":
            shape = (
                f'<circle cx="{x:.1f}" cy="{y:.1f}" r="8" fill="#111">'
                f"<title>{title}</title></circle>"
            )
        elif kind == "reference":
            shape = (
                f'<rect x="{x - 7:.1f}" y="{y - 7:.1f}" width="14" height="14" '
                f'fill="#7c3aed"><title>{title}</title></rect>'
            )
        else:
            rank = int(point.get("rank", 0))
            shape = (
                f'<a href="#candidate-{rank}" data-candidate-rank="{rank}" '
                f'aria-label="Review candidate {rank}">'
                f'<circle cx="{x:.1f}" cy="{y:.1f}" r="11" fill="#0ea5e9">'
                f"<title>{title}</title></circle>"
                f'<text x="{x:.1f}" y="{y + 4:.1f}" text-anchor="middle" '
                f'font-size="11" fill="white" pointer-events="none">{rank}</text></a>'
            )
        shapes.append(shape)
    return (
        '<div class="diverge-map">'
        f'<svg viewBox="0 0 {width} {height}" role="img" aria-label="Diverge embedding map">'
        '<rect width="100%" height="100%" rx="12" fill="#f8fafc"/>'
        + "".join(shapes)
        + "</svg><p>● source &nbsp; ■ reference &nbsp; "
        + "<span style='color:#0ea5e9'>●</span> candidates</p></div>"
    )


def render_navigation_html(candidates: list[dict]) -> str:
    links = "".join(
        f'<a href="#candidate-{int(candidate["rank"])}" '
        f'data-candidate-rank="{int(candidate["rank"])}">{int(candidate["rank"])}</a>'
        for candidate in candidates
    )
    return f'<nav class="candidate-nav" aria-label="Candidate navigation">{links}</nav>'


def render_candidate_navigation(rank: int, count: int) -> str:
    previous = max(1, rank - 1)
    following = min(count, rank + 1)
    return (
        '<nav class="candidate-step-nav">'
        f'<a href="#candidate-{previous}" data-candidate-rank="{previous}">← Previous</a>'
        f'<a href="#candidate-{following}" data-candidate-rank="{following}">Next →</a>'
        "</nav>"
    )


def candidate_path(bundle: ReviewBundle, candidate: dict) -> Path:
    path = Path(candidate["path"])
    if not path.is_absolute():
        local = bundle.run_dir / path.name
        path = local if local.exists() else path.resolve()
    return path


def score_markdown(candidate: dict) -> str:
    locks = ", ".join(f"{name} {value:.2f}" for name, value in candidate["locks"].items())
    return (
        f"**Reference:** {candidate['ref_fit']:.2f} · **Locks:** {locks} · "
        f"**Novelty:** {candidate['novelty']:.2f} · **Taste:** {candidate['taste']:.2f} · "
        f"**Utility:** {candidate['utility']:.2f}"
    )


def record_choice(
    bundle: ReviewBundle,
    candidate: dict,
    label: str,
    choices_path: str | Path,
    embedder: Embedder,
) -> str:
    path = candidate_path(bundle, candidate)
    embedding = embedder.embed_file(path)
    add_choice(path, embedding, label, choices_path)
    return f"Recorded **{label}** for candidate {candidate['rank']}."


def record_taste_choice(
    bundle: ReviewBundle,
    candidate: dict,
    label: str,
    events_path: str | Path,
    embedder: Embedder,
) -> TasteEvent:
    if label not in {"love", "keep", "discard", "export"}:
        raise ValueError("unsupported absolute taste label")
    path = candidate_path(bundle, candidate)
    config = bundle.manifest.get("config", {})
    if not profile_settings(events_path).get("learning_enabled", True):
        raise RuntimeError("taste learning is disabled in the local profile")
    source_path = config.get("source")
    source_embedding = (
        embedder.embed_file(Path(source_path)).tolist()
        if source_path and Path(source_path).exists()
        else None
    )
    references = config.get("references") or []
    reference_paths = [Path(item[0]) for item in references if Path(item[0]).exists()]
    reference_embeddings = embedder.embed_batch(reference_paths).tolist() if reference_paths else []
    record = CandidateRecord.from_embedding(
        path,
        embedder.embed_file(path),
        {
            "source_embedding": source_embedding,
            "reference_embeddings": reference_embeddings,
            "reference_weights": [item[1] for item in references if Path(item[0]).exists()],
            "scores": {
                "novelty": candidate.get("novelty", 0.0),
                "self_novelty": candidate.get("self_novelty", 0.0),
                "lock_score": candidate.get("lock_score", 0.0),
                **candidate.get("locks", {}),
            },
            "descriptors": candidate.get("descriptors", {}),
        },
    )
    return TasteEventStore(events_path).append(
        TasteEvent(
            event_type="export" if label == "export" else "absolute",
            label=label,
            candidate_a=record,
            batch_id=bundle.run_dir.name,
            source_path=config.get("source"),
            run_config={key: config.get(key) for key in ("transform", "spread", "drift", "locks")},
        )
    )


def record_pairwise_choice(
    bundle: ReviewBundle,
    candidate_a: dict,
    candidate_b: dict,
    label: str,
    events_path: str | Path,
    embedder: Embedder,
) -> TasteEvent | None:
    if label == "skip":
        return None
    if label not in {"prefer_a", "prefer_b", "neither"}:
        raise ValueError("unsupported pairwise label")
    if not profile_settings(events_path).get("learning_enabled", True):
        raise RuntimeError("taste learning is disabled in the local profile")
    a_path = candidate_path(bundle, candidate_a)
    b_path = candidate_path(bundle, candidate_b)
    config = bundle.manifest.get("config", {})
    record_a = CandidateRecord.from_embedding(a_path, embedder.embed_file(a_path))
    record_b = CandidateRecord.from_embedding(b_path, embedder.embed_file(b_path))
    pair_key = frozenset((record_a.embedding_hash, record_b.embedding_hash))
    store = TasteEventStore(events_path)
    for event in store.load():
        if event.event_type == "pairwise" and event.candidate_a and event.candidate_b:
            previous = frozenset(
                (event.candidate_a.embedding_hash, event.candidate_b.embedding_hash)
            )
            if previous == pair_key:
                return None
    return store.append(
        TasteEvent(
            event_type="pairwise",
            label=label,
            candidate_a=record_a,
            candidate_b=record_b,
            batch_id=bundle.run_dir.name,
            source_path=config.get("source"),
            run_config={key: config.get(key) for key in ("transform", "spread", "drift", "locks")},
        )
    )


def train_taste(events_path: str | Path, model_path: str | Path) -> dict:
    model = TasteModel()
    report = model.fit(training_events(events_path))
    model.save(model_path)
    return report.__dict__


KEYBOARD_JS = """
() => {
  let selected = 0;
  const rows = () => Array.from(document.querySelectorAll('.candidate-row'));
  const select = (next) => {
    const items = rows();
    if (!items.length) return;
    selected = Math.max(0, Math.min(items.length - 1, next));
    items.forEach((item, i) => item.classList.toggle('keyboard-selected', i === selected));
    items[selected].scrollIntoView({behavior: 'smooth', block: 'center'});
  };
  document.addEventListener('click', (event) => {
    const link = event.target.closest('[data-candidate-rank]');
    if (link) select(Number(link.dataset.candidateRank) - 1);
  });
  document.addEventListener('keydown', (event) => {
    if (['INPUT', 'TEXTAREA'].includes(document.activeElement?.tagName)) return;
    if (event.repeat) return;
    if (event.key === 'j') select(selected + 1);
    if (event.key === 'k') select(selected - 1);
    if (event.key === 'l') rows()[selected]?.querySelector('.love-button')?.click();
    if (event.key === 'y') rows()[selected]?.querySelector('.keep-button')?.click();
    if (event.key === 'n') rows()[selected]?.querySelector('.discard-button')?.click();
    if (event.key === 'u') rows()[selected]?.querySelector('.undo-button')?.click();
  });
  setTimeout(() => select(0), 500);
}
"""

CSS = """
.diverge-map svg { width: 100%; max-height: 420px; }
.diverge-map a { cursor: pointer; }
.candidate-nav { position: sticky; top: 8px; z-index: 20; display: flex; gap: 8px;
  justify-content: center; padding: 10px; margin: 8px 0; border-radius: 12px;
  background: color-mix(in srgb, var(--block-background-fill) 92%, transparent);
  box-shadow: 0 4px 16px rgb(15 23 42 / 12%); backdrop-filter: blur(8px); }
.candidate-nav a, .candidate-step-nav a { display: inline-flex; align-items: center;
  justify-content: center; min-width: 42px; min-height: 42px; padding: 6px 12px;
  border: 1px solid #38bdf8; border-radius: 999px; color: inherit; text-decoration: none; }
.candidate-nav a:hover, .candidate-step-nav a:hover { background: #e0f2fe; }
.candidate-row { scroll-margin-top: 90px; border: 2px solid transparent;
  border-radius: 12px; padding: 8px; }
.candidate-row.keyboard-selected { border-color: #0ea5e9; background: #f0f9ff; }
.candidate-row:target { border-color: #0ea5e9; }
.candidate-step-nav { display: flex; justify-content: space-between; margin-top: 8px; }
"""


def build_app(
    run_dir: str | Path,
    *,
    choices_path: str | Path = "choices.jsonl",
    critic_model: str | Path = "models/critic.joblib",
    embedder_factory: Callable[[], Embedder] = Embedder,
    taste_events: str | Path = "taste/events.jsonl",
    taste_model: str | Path = "taste/model.joblib",
):
    import gradio as gr

    bundle = load_bundle(run_dir)
    embedder = embedder_factory()
    with gr.Blocks(title=f"Diverge review — {bundle.run_dir.name}", css=CSS, js=KEYBOARD_JS) as app:
        gr.Markdown(
            f"# Diverge review\n`{bundle.run_dir}`\n\n"
            "Keyboard: **j/k** navigate, **l** love, **y** keep, **n** discard, **u** undo."
        )
        gr.HTML(render_map_html(bundle.map_points))
        gr.HTML(render_navigation_html(bundle.candidates))
        status = gr.Markdown("Ready.")
        profile_model, profile_warning = load_or_neutral(taste_model)
        profile = profile_dict(profile_model, taste_events)
        hypotheses = infer_descriptors(TasteEventStore(taste_events).load(effective=True))
        hypothesis_text = ", ".join(item["phrase"] for item in hypotheses) or "not enough evidence"
        with gr.Accordion("Taste profile", open=False):
            gr.Markdown(
                f"**Taste:** {profile_model.observation_count} events · "
                f"confidence {profile['confidence']:.0%} · v2\n\n"
                f"Editable hypotheses: **{hypothesis_text}**"
                + (f"\n\nWarning: {profile_warning}" if profile_warning else "")
            )
            opinion = gr.Slider(
                0,
                100,
                value=int(bundle.manifest.get("taste", {}).get("opinion", 50)),
                step=1,
                label="Opinion",
            )
            learning = gr.Checkbox(
                value=bool(profile_settings(taste_events).get("learning_enabled", True)),
                label="Enable explicit taste learning",
            )
            with gr.Row():
                save_profile = gr.Button("Save profile settings")
                reset = gr.Button("Reset taste")
                export_button = gr.Button("Export profile")
            import_file = gr.File(label="Import portable model", file_types=[".joblib"])

            def save_settings(opinion_value: int, learning_value: bool) -> str:
                edit_profile(
                    taste_events,
                    opinion=int(opinion_value),
                    learning_enabled=bool(learning_value),
                )
                return "Taste profile settings saved locally."

            def reset_settings() -> str:
                reset_profile(taste_events)
                TasteModel().save(taste_model)
                return "Taste evidence reset with a recoverable history marker."

            def export_settings() -> str:
                summary = Path(taste_model).with_name("profile.json")
                portable = Path(taste_model).with_name("profile.joblib")
                export_profile(taste_model, taste_events, summary)
                export_model(taste_model, portable)
                return f"Profile exported locally to `{portable}` with summary `{summary}`."

            def import_settings(upload: object) -> str:
                if upload is None:
                    return "Choose a .joblib profile first."
                source = getattr(upload, "name", upload)
                import_model(Path(str(source)), taste_model)
                return "Portable taste model imported and validated."

            save_profile.click(save_settings, inputs=[opinion, learning], outputs=status)
            reset.click(reset_settings, outputs=status)
            export_button.click(export_settings, outputs=status)
            import_file.change(import_settings, inputs=import_file, outputs=status)
        latest_events: dict[int, str] = {}
        for candidate in bundle.candidates:
            rank = int(candidate["rank"])
            with gr.Group(elem_id=f"candidate-{rank}", elem_classes=["candidate-row"]):
                gr.Markdown(f"## Candidate {rank}\n{score_markdown(candidate)}")
                gr.Audio(value=str(candidate_path(bundle, candidate)), label=f"Candidate {rank}")
                with gr.Row():
                    love = gr.Button("Love", variant="primary", elem_classes=["love-button"])
                    keep = gr.Button("Keep", variant="primary", elem_classes=["keep-button"])
                    discard = gr.Button("Discard", variant="stop", elem_classes=["discard-button"])
                    undo = gr.Button("Undo", elem_classes=["undo-button"])

                def decide(label: str, item=candidate, item_rank=rank) -> str:
                    event = record_taste_choice(bundle, item, label, taste_events, embedder)
                    latest_events[item_rank] = event.event_id
                    # Keep the v1 log usable as a rollback artifact.
                    if label in {"keep", "discard"}:
                        record_choice(bundle, item, label, choices_path, embedder)
                    report = train_taste(taste_events, taste_model)
                    return (
                        f"Recorded **{label}** for candidate {item_rank}. Taste: "
                        f"{report['observations']} events · "
                        f"confidence {report['confidence']:.0%} · v2"
                    )

                def undo_choice(item_rank=rank) -> str:
                    event_id = latest_events.get(item_rank)
                    if not event_id:
                        return "Nothing to undo for this candidate."
                    TasteEventStore(taste_events).undo(event_id)
                    report = train_taste(taste_events, taste_model)
                    latest_events.pop(item_rank, None)
                    return f"Undone. Taste: {report['observations']} events · v2"

                love_handler = partial(decide, "love")
                keep_handler = partial(decide, "keep")
                discard_handler = partial(decide, "discard")
                undo_handler = partial(undo_choice)
                love.click(fn=love_handler, outputs=status)
                keep.click(fn=keep_handler, outputs=status)
                discard.click(fn=discard_handler, outputs=status)
                undo.click(fn=undo_handler, outputs=status)
                gr.HTML(render_candidate_navigation(rank, len(bundle.candidates)))
        if len(bundle.candidates) >= 2:
            comparison = sorted(
                bundle.candidates,
                key=lambda item: (
                    -float(item.get("taste_uncertainty", 1.0)),
                    abs(float(item.get("taste", 0.5)) - 0.5),
                    int(item["rank"]),
                ),
            )[:2]
            with gr.Group():
                gr.Markdown("## Optional comparison\nWhich direction is more you?")
                with gr.Row():
                    gr.Audio(value=str(candidate_path(bundle, comparison[0])), label="Direction A")
                    gr.Audio(value=str(candidate_path(bundle, comparison[1])), label="Direction B")
                with gr.Row():
                    prefer_a = gr.Button("A")
                    prefer_b = gr.Button("B")
                    neither = gr.Button("Neither")
                    skip = gr.Button("Skip")

                def compare(label: str) -> str:
                    event = record_pairwise_choice(
                        bundle,
                        comparison[0],
                        comparison[1],
                        label,
                        taste_events,
                        embedder,
                    )
                    if event is None:
                        return "Comparison skipped or already recorded."
                    report = train_taste(taste_events, taste_model)
                    return (
                        f"Comparison recorded. Taste: {report['observations']} events · "
                        f"confidence {report['confidence']:.0%} · v2"
                    )

                prefer_a.click(fn=partial(compare, "prefer_a"), outputs=status)
                prefer_b.click(fn=partial(compare, "prefer_b"), outputs=status)
                neither.click(fn=partial(compare, "neither"), outputs=status)
                skip.click(fn=partial(compare, "skip"), outputs=status)
        retrain = gr.Button("Retrain critic")

        def retrain_critic() -> str:
            result = train_critic(choices_path, critic_model)
            if result["trained"]:
                return (
                    f"Critic trained on **{result['n']}** choices; "
                    f"train accuracy **{result['accuracy']:.1%}**."
                )
            return f"Critic not trained: {result['n']}/{30} labeled choices available."

        retrain.click(fn=retrain_critic, outputs=status)
    return app


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--choices", type=Path, default=Path("choices.jsonl"))
    parser.add_argument("--critic-model", type=Path, default=Path("models/critic.joblib"))
    parser.add_argument("--taste-events", type=Path, default=Path("taste/events.jsonl"))
    parser.add_argument("--taste-model", type=Path, default=Path("taste/model.joblib"))
    parser.add_argument(
        "--share", action="store_true", help="Create a Gradio share link (not local-only)"
    )
    args = parser.parse_args(argv)
    if args.share:
        raise SystemExit("Diverge is local-only; Gradio share links are disabled.")
    build_app(
        args.run_dir,
        choices_path=args.choices,
        critic_model=args.critic_model,
        taste_events=args.taste_events,
        taste_model=args.taste_model,
    ).launch(inbrowser=True, share=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
