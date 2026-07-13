from __future__ import annotations

import argparse
import html
import json
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from diverge.critic import add_choice, train_critic
from diverge.embed import Embedder


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
    if (event.key === 'y') rows()[selected]?.querySelector('.keep-button')?.click();
    if (event.key === 'n') rows()[selected]?.querySelector('.discard-button')?.click();
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
):
    import gradio as gr

    bundle = load_bundle(run_dir)
    embedder = embedder_factory()
    with gr.Blocks(title=f"Diverge review — {bundle.run_dir.name}", css=CSS, js=KEYBOARD_JS) as app:
        gr.Markdown(
            f"# Diverge review\n`{bundle.run_dir}`\n\n"
            "Keyboard: **j/k** navigate, **y** keep, **n** discard."
        )
        gr.HTML(render_map_html(bundle.map_points))
        gr.HTML(render_navigation_html(bundle.candidates))
        status = gr.Markdown("Ready.")
        for candidate in bundle.candidates:
            rank = int(candidate["rank"])
            with gr.Group(elem_id=f"candidate-{rank}", elem_classes=["candidate-row"]):
                gr.Markdown(f"## Candidate {rank}\n{score_markdown(candidate)}")
                gr.Audio(value=str(candidate_path(bundle, candidate)), label=f"Candidate {rank}")
                with gr.Row():
                    keep = gr.Button("Keep", variant="primary", elem_classes=["keep-button"])
                    discard = gr.Button("Discard", variant="stop", elem_classes=["discard-button"])
                keep.click(
                    fn=lambda item=candidate: record_choice(
                        bundle, item, "keep", choices_path, embedder
                    ),
                    outputs=status,
                )
                discard.click(
                    fn=lambda item=candidate: record_choice(
                        bundle, item, "discard", choices_path, embedder
                    ),
                    outputs=status,
                )
                gr.HTML(render_candidate_navigation(rank, len(bundle.candidates)))
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
    parser.add_argument(
        "--share", action="store_true", help="Create a Gradio share link (not local-only)"
    )
    args = parser.parse_args(argv)
    if args.share:
        raise SystemExit("Diverge is local-only; Gradio share links are disabled.")
    build_app(args.run_dir, choices_path=args.choices, critic_model=args.critic_model).launch(
        inbrowser=True, share=False
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
