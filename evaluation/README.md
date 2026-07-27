# Phase 0 engine evaluation

This directory contains the reproducible inputs for the engine gate. Generated audio, reports,
blind trials, judgments, and legal-review results remain local and are ignored by Git.

## Prepare the CC0 corpus

`corpus.cc0.json` pins two assets for each target source class. Every asset records its creator,
source page, CC0 declaration, Openverse record, preview URL, and source/prepared checksums. The
source-page metadata was reviewed on the date recorded in the manifest. The script refuses changed
downloads and produces 44.1 kHz float WAVs under the ignored `data/cc0/` directory.

```bash
.venv/bin/python scripts/prepare_evaluation_corpus.py
```

The corpus uses Freesound's high-quality previews because original-file downloads require a
Freesound account. This is suitable for a controlled same-input engine comparison, but counsel and
the study owner should decide whether original lossless files are required before treating the gate
as final.

## Freeze the hardware classification

Use `--hardware-tier minimum` only on the exact supported minimum Mac. Other machines must be
classified as `reference` or left `unclassified`. The report records model identifier, processor,
memory, OS, and Python version without recording a serial number.

The benchmark reports the first generation as cold and all later generations as warm. The latency
gate requires the cold result and warm P95 to meet the product budget. A reference-machine pass
does not satisfy the minimum-hardware gate.

## Run and compare

```bash
.venv/bin/diverge benchmark --corpus evaluation/corpus.cc0.json \
  --engine open-small --fast --hardware-tier reference
.venv-sa3/bin/diverge benchmark --corpus evaluation/corpus.cc0.json \
  --engine sa3-small-music --fast --hardware-tier reference
.venv-sa3/bin/diverge benchmark --corpus evaluation/corpus.cc0.json \
  --engine sa3-small-sfx --fast --hardware-tier reference
.venv/bin/diverge compare-benchmarks \
  evaluation/reports/open-small/benchmark.json \
  evaluation/reports/sa3-small-music/benchmark.json \
  evaluation/reports/sa3-small-sfx/benchmark.json \
  --baseline open-small --output-dir evaluation/comparison
```

Give reviewers `blind_trials.json` and `blind_audio/`, but not `blind_answer_key.json`. Each trial's
`judgments` list accepts entries like:

```json
{"reviewer_id": "reviewer-01", "winner": "a", "notes": "useful and source-related"}
```

`winner` must be `a`, `b`, `tie`, or `neither`. A reviewer can judge each trial once.

Copy `legal-review.template.json` outside version control and have counsel fill it. `approved` is
accepted only when `reviewed_by`, `reviewed_at`, and `scope` are also present. Then calculate the
gate without interpreting automated similarity as a listening verdict:

```bash
.venv/bin/diverge evaluate-benchmarks \
  --comparison-dir evaluation/comparison \
  --legal-review evaluation/legal-review.json
```

`decision.json` selects an engine only when the corpus, four-times speedup, minimum-hardware
latency, blind non-inferiority, full lock-safe set, and redistribution gates all pass.
