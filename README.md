# Diverge

Diverge is a local-first variation instrument for producers. Give it a source sound, choose what
must stay recognizable, and it returns a small, varied set that can be auditioned, kept, exported,
or used as the start of another batch.

The Python engine and JUCE AU/VST3/Standalone plugin are implemented for Apple Silicon. Model
weights, user audio, taste data, and evaluation reports stay local and are not committed.

## Development

```bash
uv sync --extra dev
uv run pytest -m "not slow"
uv run diverge run --mock --source data/loop_a.wav --ref data/ref_a.wav:1 \
  --transform 40 --spread 60 --drift 30 --locks groove
```

Real generation uses Stable Audio Open Small. Accept its license, set `HF_TOKEN`, then run:

```bash
uv sync --extra dev --extra real
uv run python scripts/download_models.py
uv run diverge run --source data/loop_a.wav --ref data/ref_a.wav:1 --fast
```

Normal mode generates 32 candidates. Fast mode generates 16. Both request up to eight displayed
results. Preserve and quality checks are hard gates: if fewer than eight candidates pass, Diverge
returns the valid subset and records the shortfall instead of weakening the constraints.

Outputs default to the source's exact duration. Explicit duration changes are recorded as crops or
loop fills. Silence, clipping, invalid layouts, severe discontinuities, and wrong-length candidates
are rejected before selection. Mono sources produce mono files; stereo sources remain stereo.

## Plugin

Build, test, and host workflow instructions are in [plugin/README.md](plugin/README.md).

The current product direction, release gates, and acceptance criteria are in
[PLUGIN_PRODUCT_SPEC.md](PLUGIN_PRODUCT_SPEC.md).

## Taste

Taste v2 learns from explicit local choices and comparisons. Its event history is append-only.
Hard quality and Preserve gates run before taste affects ordering.

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
