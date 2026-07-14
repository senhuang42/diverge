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

Normal mode generates 32 candidates and selects eight. `--fast` uses four diffusion steps
and a 16-candidate pool for iteration; pass `--n-oversample 32` to retain the full pool.

Review a completed batch locally with clickable map points, numbered navigation, or the
same arrow/Favorite/Keep/Pass shortcuts used by the plugin:

```bash
uv run python -m review.app runs/<timestamp>
```

The JUCE plugin build and host workflow are documented in [plugin/README.md](plugin/README.md).

All audio is loaded and written as 44.1 kHz stereo float WAV. Outputs live under `runs/`.

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
