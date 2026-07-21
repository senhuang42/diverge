# Diverge plugin offering: research, product direction, and high-level specification

Research snapshot: July 21, 2026

## Executive verdict

Diverge should continue, but it should not ship as merely a local AI sample generator. That
category has commoditized quickly. Splice, Output, Audialab, Daydream, OBSIDIAN Neural, and
several smaller products now generate or transform samples inside a DAW; some are local, some
are fast, and several already offer source-derived variations.

Diverge's defensible product is narrower:

> **A controlled variation instrument that turns a producer's own session fragment into a small,
> meaningfully different set while preserving the musical properties they choose.**

The product should win on the quality of the exploration loop, not on access to a model:

1. Capture a real fragment from the current session.
2. State what must survive and how far the fragment may move.
3. Receive a non-redundant set that honors that contract.
4. Compare it to the source in time and at matched loudness.
5. Use, reject, or branch from a result without losing the session's context.
6. Improve ordering from explicit, local choices over time.

The existing repository has much of the right workflow and a strong visual foundation. The next
work should focus on engine modernization, musical truth, host integration, and distributability.
Another general UI overhaul would have lower value.

## 1. Product intent

### Primary user

An electronic producer, beatmaker, media composer, or sound designer working on Apple Silicon in
Logic Pro or Ableton Live. They already have a one-shot, texture, recording, or 1-8 bar fragment
worth keeping, but it is too literal, familiar, or incomplete. They want surprise without giving
up authorship or leaving the DAW.

### Job to be done

When a promising sound is stuck, help the producer explore controlled alternatives and place a
useful result back into the session before momentum is lost.

### Product promise

**Keep what matters. Change what does not. Hear the useful possibilities.**

### What it is not

Diverge is not a prompt-to-song service, sample marketplace, autonomous composer, real-time audio
effect, or replacement for detailed synthesis. It is a pull-based, generate-then-curate instrument
for transforming audio the producer supplies.

## 2. Current offering assessment

### What is already valuable

- The source-first concept fits an active production session better than blank-page prompting.
- `Change` and `Preserve Groove / Melody / Timbre` express an understandable creative contract.
- Oversample-and-select is a better batch strategy than showing the first eight random seeds.
- The result grid, instant card selection, A/B action, Keep/Pass/Favorite, drag to DAW, branch,
  history, and optional map form a coherent workflow.
- The generation job is outside the audio thread and survives editor closure.
- Taste v2 uses explicit local decisions, contextual features, uncertainty, migration, undo, and
  inspectable artifacts rather than covert telemetry.
- Run bundles and manifests make generation reproducible and debuggable.
- The engineering baseline is credible: all 57 non-slow Python tests, Ruff, and the C++
  `DivergeJobRunnerLifecycle` test passed during this review.

### Gaps that materially weaken the promise

| Severity | Gap | Evidence in the current product | Required response |
| --- | --- | --- | --- |
| Release blocker | Generation is based on an obsolete latency/quality baseline | The current Stable Audio Open Small path took about 87 seconds for a fast 16-to-8 run in repository calibration. Stable Audio 3 Small now reports seconds-level Apple Silicon generation and supports audio-to-audio, inpainting, and continuation. | Replace the default engine after a measured quality/performance bake-off. Target a complete useful batch in seconds, not minutes. |
| Release blocker | `Preserve` is not yet a trustworthy contract | Selection silently relaxes lock thresholds until eight candidates survive. A reviewed real manifest requested `0.55` and returned candidates after relaxing to `0.40`. | Never silently violate a preserve constraint. Generate more, return fewer, or explicitly ask the user to relax it. Validate each preserve dimension perceptually. |
| Release blocker | Reference steering is weaker than the UI implies | `StableAudioGenerator.generate()` discards `style_embedding`; reference audio affects post-generation CLAP ranking, while model conditioning uses text, sometimes inferred from a filename. | Prove that a reference materially shifts a batch in blind tests. Implement real conditioning or honest analysis-plus-ranking; otherwise demote or rename the feature. |
| Release blocker | The plugin is not a distributable product | Defaults point to the checkout's Python, models, choices, and runs paths. Model setup requires a separate environment, script, gated download, and token. | Ship a signed backend/runtime and guided model manager. A normal user must never see a Python path or need a development checkout. |
| Release blocker | Audition is not a true host-aware A/B | Preview audio is added to pass-through input, is not synchronized to host transport, and is loaded without resampling to the host sample rate. At 48/96 kHz it can play at the wrong pitch and length. | Resample correctly, replace rather than layer during A/B, align to host transport when possible, and loudness-match audition only. |
| High | Inputs and outputs do not fit the session | Plugin runs are fixed to 8 seconds and 16 candidates. There is no bar/region editor, host tempo/key use, exact loop-length contract, or loop-seam check. | Capture or crop a precise region; default output to the source region's exact duration; understand bars, BPM, sample rate, mono/stereo, and loop boundaries. |
| High | Candidate explanations are not discriminative | The current heuristic can give seven cards the same explanation because it reports the largest similarity score rather than a meaningful difference from the source. | Explain why each result is in the set using validated deltas and batch roles; omit prose when it is not informative. |
| High | Used audio is not durably committed | Plugin state and recent runs refer to absolute files under `runs/`. Dragging a file does not establish a durable project-aware asset contract. | Maintain content-addressed assets, stable exports, repairable references, and a clear retention policy. Never garbage-collect used or favorited audio. |
| High | Decisions collapse distinct user signals | Candidate state is a single enum, so export can replace Keep or Favorite. Choice persistence is split between sidecars and taste events. | Model Keep, Favorite, Pass, Export, and Branch as independent append-only events with derived UI state. |
| Medium | Personal taste is sophisticated but unproven as product value | The implementation is extensive, but there is no producer study showing that learned ordering increases useful-result rate across contexts. | Keep it supporting and local. Promote it only after a chronological, blinded study shows lift over neutral ordering. |
| Medium | Library avoidance is setup-heavy and invisible when inactive | It requires a separately built index and raw path; without one novelty is neutral. | Remove it from launch scope or provide a folder picker, incremental indexing, status, and an understandable `Avoid repeats` action. |
| Medium | The map has no stable musical semantics | UMAP coordinates are batch-relative and can move between runs. It is visually interesting but does not explain what an axis means. | Keep it in diagnostics/experiments until user testing shows faster or better choices than the waveform grid. |
| Medium | Platform reach is narrow | Current build is macOS 14+, AU/VST3, Apple Silicon. Direct competitors increasingly ship Windows and sometimes AAX. | Launch narrowly if necessary, but make the backend and asset schema portable and treat Windows VST3 as the first expansion after product validation. |

## 3. Existing space

The meaningful market is not one category. Diverge sits where five adjacent jobs overlap.

| Product / category | What it has established | Implication for Diverge |
| --- | --- | --- |
| [Splice Sounds Variations](https://splice.com/blog/introducing-splice-sounds-plugin-beta/) | Five source-derived variations in an AU/VST3 plugin, with session key/tempo and harmony/rhythm complexity. Each licensed variation costs a credit and begins from a human-made Splice sound. | Source variation, DAW drag-out, musical fit, and responsible-training language are table stakes. Diverge can still own arbitrary user audio, offline use, no credits, and deeper preservation/iteration. |
| [Output Co-Producer Re-imagine](https://support.output.com/en/articles/10628997-co-producer-user-guide) | Audio/text search, tempo-synced audition, similar-sound exploration, ratings, saved/dragged history, and unlimited transformations of Output's catalog. | `More like this`, rating, history, and in-context audition are expected. A catalog is not a feasible wedge for Diverge. |
| [Audialab Emergent Drums](https://audialab.net/) | Local-focused, one-time-purchase plugins; Deep Sampling accepts a sound and exposes similarity-based variations, primarily for drums. | Local/no-subscription positioning alone is not unique. Diverge must be broader than drums and more musically controllable than one similarity slider. |
| [Daydream](https://daydream.live/) | Claims any-source live remixing, prompt blending, continuous control, sub-100 ms parameter response, a DAW plugin, and an open engine that can be self-hosted on a powerful NVIDIA GPU. | Daydream owns the emerging live/performance framing. Diverge should own deliberate offline exploration, constraint fidelity, curation, and personal selection on Apple Silicon. |
| [OBSIDIAN Neural Local Edition](https://obsidian-neural.com/local.php) | Stable Audio 3 Medium locally on CPU, roughly 11-second generation, AU/VST3/standalone, one-time pricing around EUR 29. | Fast local generation is now a baseline and creates strong price pressure. Model access cannot justify premium pricing. |
| [Sononym](https://www.sononym.net/docs/manual/similarity-search/) | No generation, but excellent region selection, audio similarity by understandable aspects, live recording, query history, pitch control, and direct region drag-out. | Mature sample tools set the interaction standard. Diverge needs a real region editor and should use understandable musical dimensions rather than an opaque map. |
| [Stable Audio 3](https://github.com/Stability-AI/stable-audio-3) | Open-weight Small/Medium models with audio-to-audio, inpainting, continuation, LoRA support, CPU/CoreML paths, and claimed seconds-level generation on a Mac. | The current engine should be treated as replaceable infrastructure. Inpainting and continuation enable a much deeper second-generation workflow. |
| Full-song tools | Suno/Udio-style products optimize for prompt-to-composition, vocals, and finished tracks. | Stay out. Competing there would erase Diverge's producer-tool identity and require a different product. |

Two research findings reinforce this direction. Professional production workflows value speed,
controllability, and creative agency, and tensions arise when automation weakens those qualities
([McClellan & Morreale, 2026](https://arxiv.org/abs/2605.29931)). Studies of co-creative music
systems find novelty and surprise appealing but repeatedly identify predictability and control as
limitations ([Tchemeube et al., 2025](https://arxiv.org/abs/2504.14071)). Diverge's preservation
and curation thesis is therefore relevant, but only if those controls are perceptually true.

## 4. Strategic position

### Category

**Controlled audio variation instrument**

Avoid leading with `AI`, `diffusion`, `sampler`, or `taste model`. Those describe implementation
or adjacent categories. Lead with the job and the owned input.

### Differentiating claim

> From your sound, Diverge creates eight useful directions that are different from each other and
> recognizable in the ways you choose.

This claim contains four testable differentiators:

1. **Your sound:** arbitrary user-owned audio, not only a vendor catalog.
2. **Recognizable by choice:** explicit, verified preservation dimensions.
3. **Different from each other:** quality-diversity selection rather than random seeds.
4. **Useful directions:** session-fit, rapid A/B, durable DAW handoff, and iterative branching.

Local/private execution and explicit taste learning support the claim but are not sufficient
positioning by themselves.

### Commercial hypothesis

Validate a one-time license around USD 69-99 with no generation credits and optional paid major
upgrades. That is above narrow local generators but below broad plugin bundles. Do not lock price
until producer testing demonstrates that constraint fidelity and curation save meaningful time.

## 5. High-level product specification

### 5.1 Experience principles

1. **The source is the hero.** The UI begins with the exact session region, not a prompt.
2. **Preserve is a contract.** The product does not quietly weaken constraints to fill a grid.
3. **Speed protects agency.** First useful audio should arrive while the producer still remembers
   why they opened the plugin.
4. **A batch must have a point.** Results should cover meaningfully different directions, not
   cosmetic seed differences.
5. **Audition in context.** Playback must be host-correct, synchronized, loudness-matched, and
   one action away from source comparison.
6. **Every result can continue.** Use, branch, refine, and repair are first-class outcomes.
7. **Intelligence stays accountable.** Preferences are explicit, local, optional, reversible, and
   never stronger than the current creative brief.
8. **No runtime administration.** Installation and model management feel like a plugin product,
   not a Python project.

### 5.2 Core workflow

#### A. Capture and prepare

- Accept drag/drop, file choose, and host-aligned capture.
- Support mono and stereo source audio from 0.25 to 30 seconds at common host sample rates.
- Display a waveform region editor with crop, trim, play, seek, and optional loop mode.
- For host capture, offer 1, 2, 4, or 8 bars and begin/end on musical boundaries.
- Read host BPM, time signature, sample rate, and transport position when available.
- Detect BPM and key as hints, clearly distinguishing detected values from host facts.
- Default result duration to the selected source region. Never silently tile an input without
  showing that behavior.

#### B. Set the brief

- `Change` remains the primary control with Familiar, Evolved, and Wild anchors.
- `Preserve` offers Groove, Melody, and Timbre only where the source supports them. Tooltips define
  what each control can and cannot guarantee.
- An optional `Direction` accepts one reference audio file or a short text description.
- Hide model, sampler, seed, oversampling, CLAP, and utility settings from normal use.
- Warn about contradictory briefs such as maximum Change with all dimensions preserved, but allow
  an expert to try them.

#### C. Create

- Return the first playable candidate progressively, then fill the curated set.
- Generate an adaptive pool, run quality checks, enforce constraints, deduplicate, and select up
  to eight directions.
- If eight valid results cannot be found within the generation budget, show the valid subset and
  offer `Try more` or an explicit constraint relaxation.
- A job survives editor closure, project save/reopen, and recoverable backend failure.
- Progress reports real stages and completed work; it does not invent an exact ETA.

#### D. Explore and audition

- Candidate cards show waveform, playhead, decision state, and one useful reason for inclusion.
- Clicking a candidate selects and plays it; switching candidates is gapless and exclusive.
- Source A/B replaces candidate playback at the same musical position and matched audition
  loudness. It must not layer on top of the live source.
- If the host is playing, audition begins on an appropriate beat/bar boundary when the host permits.
- `Keep`, `Pass`, and `Favorite` are independent, reversible choices.
- `Use in DAW` creates/retains a stable named asset and records Export without erasing other choices.
- Map view is absent from the default 1.0 workflow unless comparative testing proves it improves
  selection speed or quality.

#### E. Iterate and recover

- `More like this` recenters generation on a selected candidate and preserves lineage.
- After hearing a batch, offer contextual `Closer`, `Wider`, and preserve adjustments for the next
  batch instead of exposing selector weights.
- Recent history restores source, brief, candidates, choices, exports, and branches.
- Missing or moved assets trigger a repair flow. Used and favorited files are protected from
  cleanup.
- Region inpainting/repair and continuation are a 1.x priority once the Stable Audio 3 path is
  proven; they should reuse the same source editor and lineage model.

#### F. Preferences

- Learning is off or neutral until there is enough evidence to improve a decision.
- Use only explicit events and contextual features; no passive monitoring or network telemetry.
- Provide pause, reset-with-recovery, import, export, and plain-language tendencies.
- The current brief and hard constraints always outrank taste.
- Do not claim personalization improves results until an offline chronological evaluation and a
  blinded producer study both show lift.

### 5.3 Selection contract

The selector is the center of the differentiated product and needs an explicit contract.

1. **Technical quality gate:** reject silence, clipping, severe discontinuities, invalid channel
   layouts, low reconstruction quality, and obvious loop-seam failures.
2. **Session-fit gate:** result duration is sample-accurate; tempo/key requirements are measured
   where requested.
3. **Preserve gate:** each active dimension must meet a source-class-calibrated threshold. No
   automatic relaxation is invisible to the user.
4. **Direction response:** when direction is present, candidates must measurably move toward it
   relative to a no-direction baseline without collapsing source identity.
5. **Deduplication:** reject candidates that are near-duplicates in both semantic embedding and
   relevant temporal/pitch features.
6. **Coverage:** select a Pareto-balanced set across quality, source identity, direction fit, and
   meaningful diversity. Always reserve at most one clearly labeled wildcard.
7. **Taste ordering:** use confidence-capped taste only after the set satisfies all earlier gates.
   Taste should mostly order and allocate within a valid pool, not rescue invalid candidates.
8. **Explanations:** derive card language from measured differences, e.g. `same groove, darker
   texture` or `new rhythm, melody retained`. Suppress explanations that are not confident or
   discriminative.

### 5.4 Runtime and architecture

- Retain JUCE for AU/VST3/Standalone and keep all model work off the audio thread.
- Replace the user-configured Python interpreter with a versioned, signed local backend or helper
  service. The plugin communicates through a versioned structured protocol with progress, cancel,
  health, result, and typed error events.
- Keep the generator behind the existing protocol, but add engine capability metadata: supported
  duration, source class, audio-to-audio, inpainting, device, memory, and expected latency.
- Spike Stable Audio 3 Small Music and Small SFX first. Route by source class automatically if
  listening tests support it. Consider Medium as an optional quality tier only after measuring
  memory, speed, and host impact.
- Prefer the official Apple Silicon CPU/CoreML path where quality matches the reference
  implementation. Do not expose engine choice in the creative UI.
- Own one durable application-support library with content-addressed audio, manifests, event log,
  and a small indexed metadata database. Plugin state stores stable IDs and enough recovery data,
  not only absolute paths.
- Resample at system boundaries, preserve original source metadata, and render preview audio at
  the host's current sample rate.
- Throttle or pause generation when it threatens real-time DAW audio. Generation failure must not
  glitch, mute, or crash the host.

### 5.5 Installation, privacy, and rights

- Provide a signed/notarized installer for the plugin and helper. First launch performs a health
  check and a resumable model download with size, checksum, license, and storage location.
- Do not require a shell, `uv`, Python knowledge, a repository checkout, or raw Hugging Face token
  entry in the normal path.
- Runtime networking is disabled after installation unless the user explicitly checks for an
  update or downloads another model.
- Clearly state that source audio, references, choices, embeddings, and output remain local.
- Publish the model/training-data provenance and the exact commercial rights boundary. Stable
  Audio 3 uses the Stability AI Community License, which is free for commercial organizations
  under USD 1M annual revenue and requires enterprise licensing above that threshold
  ([license summary](https://stability.ai/license)). Distribution terms, Gemma components,
  attributions, and the experience for users above the threshold require counsel before sale.
- A one-click `Export diagnostics` package must omit source audio and taste data by default.

### 5.6 Launch scope

#### Must ship in 1.0

- Stable Audio 3 or another measured seconds-level local engine.
- Drag/drop and host-aligned region capture with exact output duration.
- Correct mono/stereo and 44.1/48/88.2/96 kHz behavior.
- Change, trustworthy Preserve, and one optional Direction.
- Curated grid of up to eight, true source A/B, Keep/Pass/Favorite, stable drag/export.
- More like this, recent history, lineage, and crash/editor-close recovery.
- Packaged runtime/model setup, AU/VST3/Standalone, signed/notarized macOS delivery.
- Explicit local preference events; neutral behavior until evidence is sufficient.

#### Should follow in 1.x

- Waveform-region inpainting and continuation.
- Guided library indexing and `Avoid repeats`.
- Better editable preference hypotheses and optional pairwise calibration.
- Optional high-quality model tier after resource testing.
- Windows VST3 if the core workflow validates.

#### Explicitly later or out of scope

- Prompt-to-full-song, realistic lead vocals, stem generation, autonomous arrangement, social
  sharing, cloud accounts/credits, telemetry, marketplace, AAX, live sub-100 ms generation,
  user-facing LoRA training, and model/selector parameter panels.

## 6. Success and acceptance criteria

### Product truth

- In blind comparison, at least 80% of target producers correctly identify the locked dimension as
  better preserved than in an unlocked control batch for each supported source class.
- A reference-conditioned batch is judged closer to its intended direction than a no-reference or
  wrong-reference batch at least 70% of the time. If not, Direction does not ship as a headline
  feature.
- Fewer than 10% of displayed candidates are judged redundant with another displayed candidate.
- At least 4 of 5 users can explain Change, each Preserve control, Keep, Favorite, and More like this
  after one observed session without implementation terminology.

### Workflow

- Median source-to-valid-brief time is under 30 seconds, excluding source discovery.
- First playable result is available in under 10 seconds at P50 and under 20 seconds at P95 for an
  8-second source on the minimum supported Mac; the curated set is ready in under 30/60 seconds.
  Final thresholds must be confirmed by the engine spike rather than copied from vendor claims.
- A producer can audition and disposition all results, A/B the source, and use one in the DAW
  without scrolling or documentation.
- At least 60% of observed thesis-validation batches lead to Keep, Favorite, Export, or Branch.

### Reliability

- No audio-thread deadline misses attributable to the helper in a stressed DAW session.
- Exact-duration output and correct-pitch preview at every supported sample rate.
- Project save/reopen, editor close/reopen, plugin instance removal, backend crash, cancellation,
  low disk, corrupt input, model mismatch, and moved storage all have tested outcomes.
- `auval` passes; Logic AU and Ableton VST3 complete the full capture-create-audition-export loop.
- The entire automated non-slow Python suite, lint, C++ model/service tests, and deterministic UI
  fixtures pass without model weights.

### Taste validation

- Chronological offline evaluation beats neutral ranking without increasing constraint failures or
  reducing batch diversity.
- In a blinded crossover study after a realistic warm-up period, personalized ordering produces a
  statistically credible lift in first-three audition Keep/Export rate. Otherwise it remains an
  experimental preference, not marketing.

## 7. Recommended delivery sequence

### Phase 0: revalidate the technical thesis

Build a representative, rights-cleared corpus across drums, melodic loops, bass, recorded
instruments, textures, and one-shots. Add old-engine, Stable Audio 3 Small Music, Small SFX, and
optionally Medium adapters. Measure latency, memory, source identity, preserve adherence,
direction response, seam quality, and blind preference.

Gate: select an engine only if it improves speed by at least 4x, is preferred or tied for useful
audio quality, and supports a legally distributable path. If no engine makes Preserve and
Direction truthful, narrow the product before proceeding.

### Phase 1: make the current experience session-correct

Implement host/sample-rate-safe preview, true A/B, exact region/duration handling, mono/stereo,
quality gates, non-relaxing preserve behavior, independent decision events, and durable assets.
Reuse the current design system and result grid.

Gate: a deterministic host matrix and five observed producer sessions complete without wrong
pitch, layering, lost assets, misleading preserve behavior, or manual file repair.

### Phase 2: make it installable

Package the helper and model manager, remove development-path dependencies, implement health and
upgrade/rollback, complete license notices, sign/notarize, and test clean-machine installation.

Gate: a producer on a clean supported Mac goes from installer to first local batch without a shell,
raw path, repository, or developer assistance.

### Phase 3: prove the differentiated loop

Polish adaptive set construction, discriminative explanations, More like this, lineage, recent
recovery, and taste ordering. Add region inpainting only after the core loop meets latency and
truth gates.

Gate: in 12-15 target-producer sessions, the workflow and useful-result thresholds above are met,
and participants choose Diverge's controlled batch over a raw generator for the intended job.

### Phase 4: commercial beta

Run a paid or deposit-backed pricing test, ship a notarized beta, validate support burden and model
licensing, and decide Windows from observed demand. Do not use hidden analytics; use opt-in study
exports and direct interviews.

## 8. Immediate next increment

Do not add more creative controls yet. The next cohesive increment should be a time-boxed Stable
Audio 3 evaluation adapter and benchmark harness that can answer four questions with evidence:

1. Can a full valid batch finish under the proposed latency budget on minimum hardware?
2. Does audio-to-audio quality beat the current Open Small path across the target source classes?
3. Can Groove, Melody, and Timbre preservation be enforced without silently weakening them?
4. Can the selected engine and its text conditioner be redistributed in a paid plugin under an
   acceptable install/license flow?

Those answers determine the real 1.0 scope. The current visual workflow is already good enough to
test it.
