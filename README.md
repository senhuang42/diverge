# Diverge

Diverge is a local-first generative sampler for producers. It transforms source audio into
a deliberately varied batch, steered by reference tracks, musical locks, library novelty,
and a contextual Taste v2 profile trained from explicit local decisions and comparisons.

## Status

The Phase 1 pipeline, JUCE AU/VST3/Standalone shell, and Phase 3 taste loop are implemented.
Model weights and user audio never enter version control.
Runtime inference is local; only `scripts/download_models.py` makes network requests.

## Quick start

```bash
uv sync --extra dev
uv run pytest -m "not slow"
uv run diverge run --mock --source data/loop_a.wav --ref data/ref_a.wav:1 \
  --transform 40 --spread 60 --drift 30 --locks groove
```

For real generation, accept the Stable Audio Open Small license, set `HF_TOKEN`, and run:

```bash
uv sync --extra dev --extra real
uv run python scripts/download_models.py
uv run diverge run --source data/loop_a.wav --ref data/ref_a.wav:1 --fast
```

## Stable Audio 3 evaluation

The Phase 0 adapter supports the official `small-music` and `small-sfx` audio-to-audio APIs and
records their capabilities and licensing status in every manifest. It is an evaluation path, not
the packaged 1.0 backend: the upstream package currently requires a newer NumPy/Transformers stack
than Diverge's pinned Open Small environment, so keep it in a dedicated virtual environment.

```bash
uv venv .venv-sa3 --python 3.11
uv pip install --python .venv-sa3/bin/python \
  "git+https://github.com/Stability-AI/stable-audio-3.git@124e8a799f57a1f665495ecb72e547d0a62867f1" \
  librosa joblib scikit-learn scipy umap-learn
uv pip install --python .venv-sa3/bin/python --no-deps -e .
HF_TOKEN=... .venv-sa3/bin/diverge run --engine sa3-small-music \
  --source data/loop_a.wav --style-hint "electronic drum loop" --fast
```

Use `sa3-small-sfx` for one-shots, textures, and effects. Routing is deliberately explicit until
listening tests prove automatic source classification. The adapter uses prompt conditioning and
source audio-to-audio initialization; it does not claim that a Direction audio embedding directly
conditions the model. Model download and raw-token setup remain evaluation-only shortcomings that
the packaged helper/model manager must remove before release.

Run the same corpus once per engine to produce comparable, machine-readable evidence:

```bash
# Existing environment / baseline engine
.venv/bin/diverge benchmark --corpus evaluation/corpus.example.json \
  --engine open-small --fast

# Dedicated Stable Audio 3 environment
.venv-sa3/bin/diverge benchmark --corpus evaluation/corpus.example.json \
  --engine sa3-small-music --fast
```

Each `evaluation/reports/<engine>/benchmark.json` includes cold-start first-playable and full-pool
latency, peak process memory, exact-duration and audio quality failures, Groove/Melody/Timbre
similarities, the fixed Preserve threshold, valid-set shortfalls, direction/source fit, duplicate
rate, system identity, engine capabilities, and license-review status. The included corpus is only
a weight-free smoke fixture; its report explicitly lists the missing target classes. A release
decision requires a rights-cleared corpus covering all six target classes and blinded human quality
judgments—the harness marks those judgments pending rather than manufacturing a quality verdict.

After both engine runs, create neutralized A/B files and a four-question comparison report:

```bash
.venv/bin/diverge compare-benchmarks \
  evaluation/reports/open-small/benchmark.json \
  evaluation/reports/sa3-small-music/benchmark.json \
  --baseline open-small --output-dir evaluation/comparison
```

Give the listener `blind_trials.json` and the copied files under `blind_audio/`; keep
`blind_answer_key.json` hidden until judgments are complete. `comparison.json` can establish the
measured latency and Preserve results, but intentionally keeps audio quality pending until those
judgments are collected and redistribution pending until license counsel approves it.

Normal mode generates 32 candidates and selects eight. `--fast` uses four diffusion steps
and a 16-candidate pool for iteration; pass `--n-oversample 32` to retain the full pool.

Review a completed batch locally with clickable map points, numbered navigation, or the
same arrow/Favorite/Keep/Pass shortcuts used by the plugin:

```bash
uv run python -m review.app runs/<timestamp>
```

The JUCE plugin build and host workflow are documented in [plugin/README.md](plugin/README.md).

Pipeline audio is analyzed and written as 44.1 kHz stereo float WAV. Output duration defaults to
the source's exact duration; an explicit `--duration` is recorded as cropped or loop-filled in the
manifest. Silence, clipping, invalid layouts, severe discontinuities, and wrong-length candidates
are rejected before Preserve selection. Outputs live under `runs/`.

## Local taste profile

Taste v2 learns immediately from Favorite, Keep, and Pass decisions, supports optional
pairwise comparisons, and records append-only events under `taste/events.jsonl`. Preference
state, embeddings, model artifacts, and evaluation reports remain local and are ignored by
git. Recent-keeps anti-repetition remains separate from preference prediction.

```bash
# Preserve every usable v1 choice without rewriting choices.jsonl
uv run diverge taste migrate

# Inspect, train, evaluate chronologically, and export an inspectable summary
uv run diverge taste status
uv run diverge taste train
uv run diverge taste evaluate
uv run diverge taste export-profile
```

Generation uses one immutable Taste v2 snapshot per batch. `--opinion 0` disables taste
influence while explicit feedback can still be collected; higher values increase only the
evidence- and confidence-supported component. Hard locks are applied first. When a full
oversampled pool is available, eight results are divided between favorites, informative
comparisons, exploration, and one novelty-oriented surprise.

If `taste/model.joblib` is missing or incompatible, generation falls back to neutral scoring
and records a warning in the manifest. The original `choices.jsonl` and
`models/critic.joblib` remain valid rollback artifacts. Delete neither to reset Taste v2;
use the review panel's reset action, which appends a recoverable history marker.

## Explicit non-goals

No real-time/audio-thread inference, Windows, AAX, negative-guidance steering, diffusion
audio-embedding adapters, catalog training/LoRA, stem separation, cloud rendering, or
telemetry.
