# Diverge

Diverge is a local-first generative sampler for producers. It transforms source audio into
a deliberately varied batch, steered by reference tracks, musical locks, library novelty,
and a taste critic trained from local keep/discard decisions.

## Status

Phase 1 pipeline accepted; Phase 2 plugin work is next. Model weights and user audio never enter version control.
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

Review a completed batch locally with clickable map points, numbered navigation, or j/k/y/n:

```bash
uv run python -m review.app runs/<timestamp>
```

All audio is loaded and written as 44.1 kHz stereo float WAV. Outputs live under `runs/`.

## Explicit v1 non-goals

No real-time/audio-thread inference, Windows, AAX, negative-guidance steering, diffusion
audio-embedding adapters, catalog training/LoRA, stem separation, cloud rendering, or
telemetry.
