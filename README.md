# Diverge

Diverge is a local-first variation instrument for producers. Give it a source sound, choose how
far to move, and it returns a small, varied set that can be auditioned, kept, exported, or used as
the start of another batch.

The Python engine and JUCE AU/VST3/Standalone plugin are implemented for Apple Silicon. Model
weights, user audio, taste data, and evaluation reports stay local and are not committed.

## Development

```bash
uv sync --extra dev
uv run pytest -m "not slow"
uv run diverge run --mock --source data/loop_a.wav --ref data/ref_a.wav:1 \
  --transform 40 --spread 60 --drift 30
```

Real generation uses Stable Audio Open Small. Accept its license, set `HF_TOKEN`, then run:

```bash
uv sync --extra dev --extra real
uv run python scripts/download_models.py
uv run diverge run --source data/loop_a.wav --ref data/ref_a.wav:1 --fast
```

Normal mode generates 32 candidates. Fast mode generates 16. Both request up to eight displayed
results. Quality and pairwise uniqueness checks are hard gates: if fewer than eight candidates
pass, Diverge returns the valid subset and records the shortfall.

The plugin builds a 16-candidate model pool for up to eight displayed results. Each model candidate
receives a distinct brief. A cheap spectral-temporal check detects collapsed model batches and
retries them once with wider diffusion and the full sampler. Selection then enforces a pairwise
duplicate ceiling; it returns a smaller truthful set if eight distinct valid results do not exist.

Source and reference tracks are peers in generation. The Reference Mix control chooses a continuous
point between their full autoencoder latents: 0 is source-conditioned, 50 is an energy-matched
hybrid, and 100 is reference-conditioned. Change independently controls how far diffusion moves
from that point. Selection targets a Change-dependent distance band instead of rewarding unlimited
dissimilarity. At high Change, distance from the source, the indexed library, and recent keeps is
rewarded only after candidate-relative tonal coherence passes a hard gate. This coherence check
infers each candidate's own key or mode; it does not silently turn the optional Melody lock on.
Measured embedding, groove, and melody similarity also affect candidate ranking.
Because Open Small does not expose a native second audio-direction input, the manifest identifies
this local latent-interpolation path separately from the backend capability.

Outputs default to the selected source region's exact duration. The plugin's bar control crops both
imported and recorded sources. The Open Small backend is limited to its native 524,288-sample
(11.89-second) window; larger regions are rejected rather than time-stretched. Silence, clipping,
invalid layouts, severe discontinuities, wrong-length candidates, and duplicates are rejected
before selection. Mono sources produce mono files; stereo sources remain stereo.

## Plugin

Build, test, and host workflow instructions are in [plugin/README.md](plugin/README.md).

The current product direction, release gates, and acceptance criteria are in
[PLUGIN_PRODUCT_SPEC.md](PLUGIN_PRODUCT_SPEC.md).

## Taste

Taste v2 learns from explicit local choices and comparisons. Its event history is append-only.
Hard quality and uniqueness gates run before taste affects ordering.

```bash
uv run diverge taste status
uv run diverge taste train
uv run diverge taste evaluate
uv run diverge taste export-profile
```

## Engine evaluation

The Stable Audio 3 Small Music and Small SFX adapters are evaluation paths, not the packaged
release backend. Install Stable Audio 3 in a separate environment because its dependency stack
differs from the Open Small runtime:

```bash
uv venv .venv-sa3 --python 3.11
uv pip install --python .venv-sa3/bin/python \
  "git+https://github.com/Stability-AI/stable-audio-3.git@124e8a799f57a1f665495ecb72e547d0a62867f1" \
  librosa joblib scikit-learn scipy umap-learn
uv pip install --python .venv-sa3/bin/python --no-deps -e .
```

Run the same rights-cleared corpus through each engine, then create a blind comparison:

```bash
.venv/bin/diverge benchmark --corpus evaluation/corpus.example.json \
  --engine open-small --fast
.venv-sa3/bin/diverge benchmark --corpus evaluation/corpus.example.json \
  --engine sa3-small-music --fast
.venv/bin/diverge compare-benchmarks \
  evaluation/reports/open-small/benchmark.json \
  evaluation/reports/sa3-small-music/benchmark.json \
  --baseline open-small --output-dir evaluation/comparison
```

The included corpus is a smoke fixture. A release decision still requires a representative,
rights-cleared corpus, blind listening judgments, and license review.

## Non-goals

Diverge does not perform model inference on the audio thread. Version 1 does not target Windows,
AAX, cloud rendering, telemetry, stem separation, or real-time generation.
