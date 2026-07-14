# Diverge plugin overhaul

## Executive product verdict

Diverge has a strong product thesis and a capable engine, but the plugin currently presents
the engine's configuration and diagnostics more clearly than it presents the producer's job.
The overhaul should not add a better skin to the same control panel. It should reorganize the
product around one fast, repeatable creative loop:

> Bring an idea, choose what may change and what must survive, hear meaningfully different
> directions, then keep, use, or branch from the one that moves the track forward.

The engine's differentiators are real: source transformation rather than blank-page prompting,
reference steering, musical preservation constraints, quality-diversity selection, local taste,
and local/private execution. Those capabilities should remain. Most of their mechanics should
become invisible until a producer asks for detail.

The recommended overhaul has three product priorities:

1. Make the product understandable on sight, without knowledge of AI or the selection system.
2. Make auditioning and choosing results the visual and interaction center of the plugin.
3. Turn every useful result into the start of the next iteration, not the end of a batch.

## Experience principles

These principles outrank feature completeness and visual novelty:

1. **Audio first.** Waveforms, playback, comparison, selection, and DAW handoff receive the most
   space and the fastest interactions.
2. **Obvious in ten seconds.** The first screen asks only for a source, an optional direction, how
   much to change, and what to preserve.
3. **Intelligence without AI theater.** Say create, compare, learn, and preferences—not infer,
   embed, score, prompt, model, or confidence in the main workflow. No sparkles, robots, generic
   AI gradients, or live dashboards of internal reasoning.
4. **Smart defaults before knobs.** Diversity, taste influence, quality profile, oversampling,
   library novelty, and prompt enrichment should work automatically. Reveal an adjustment only
   when a user has a concrete reason to make it.
5. **Delight through fluency.** Delight comes from instant visual response, smooth playback,
   seamless A/B, tasteful motion, reversible choices, and the feeling that a result can be pulled
   straight into the track—not from ornamental animation.
6. **Producer language throughout.** Every label should describe a musical decision or an action
   in the session. Technical truth remains available in diagnostics without becoming the product's
   personality.

## What the product is

### Primary user

A working producer or sound designer who already has a fragment worth developing: a loop, a
recording, a hummed idea, or a rough texture. They want useful surprise without surrendering the
identity of the source or interrupting the DAW session.

This is not primarily for someone who wants to type a genre prompt and wait for a finished song.
It is not a sample browser, a mix processor, or an autonomous composition system.

### Job to be done

When an idea is promising but too literal, too familiar, or stuck, help the producer explore a
small set of controlled alternatives and get one back into the session before creative momentum
is lost.

### Product promise

**Recognizable in the ways you choose. Different in the ways that matter.**

### Defensible value

- Source-first creation fits an existing session and starts from musical intent.
- References communicate direction without requiring prompt-writing fluency.
- Preserve constraints make exploration safe enough to use on real material.
- Batch diversity avoids eight cosmetically different versions of the same answer.
- Taste learning improves ordering and selection through normal, explicit decisions.
- Local execution protects unreleased audio and avoids accounts, credits, and cloud latency.

### Core loop

```text
SOURCE -> DIRECT -> CREATE -> AUDITION -> USE / PASS -> BRANCH OR CREATE AGAIN
```

Taste learning observes explicit actions in this loop. It is supporting intelligence, not a
separate destination the user must manage.

## Current-state evaluation

The evaluation is based on the repository specification, current JUCE implementation, a live
standalone build, completed run manifests, and the browser review workflow.

### What is already strong

- The plugin has a complete end-to-end path: import or capture, generate in a child process,
  audition, label, and drag a WAV into the DAW.
- Generation is off the audio thread, and local-only execution is a meaningful trust benefit.
- The engine encodes producer-relevant concepts: amount of transformation, preservation of
  groove/melody/timbre, reference direction, and diversity.
- Eight selected results are more manageable than exposing the complete oversampled pool.
- Result bundles and manifests are inspectable and recoverable.
- Explicit decisions are preferable to covert behavioral inference.
- The map offers a potentially useful alternate way to inspect a batch.

### Where the product currently breaks its own intent

1. **The hierarchy follows implementation, not the creative task.** Source, recording,
   reference weights, locks, four knobs, text direction, render mode, generation, model state,
   a map, result numbers, metrics, six actions, and settings share one flat screen.

2. **The dominant visual object is not the sound.** The large map owns most of the window,
   including before results exist. Source, references, and candidates do not have useful
   waveforms, obvious playback states, or strong selection affordances.

3. **Auditioning is slower than it should be.** A user selects a number or map point and then
   presses a separate Audition button. There is no visible playhead, waveform scrubbing,
   source/result A/B, automatic stop-and-switch behavior, or clear playing state.

4. **Several controls are expert-only or conditionally meaningful.** `Spread`, `Drift`, and
   `Opinion` require knowledge of the selection system. `Drift` cannot rank candidates by
   library novelty when no library index is configured because every candidate receives the
   same neutral novelty value. It should not be a primary control in that state.

5. **System tuning is presented as musical intent.** Fast mode, raw reference weights, model
   confidence, Python/model/output paths, and numerical utility scores are implementation or
   diagnostic concerns. They compete with the few decisions the producer actually needs to make.

6. **Feedback semantics are unclear.** Love, Keep, Discard, Undo, Audition, and Drag WAV are
   separate actions. Keep does not visibly keep the file anywhere the producer recognizes, and
   Drag is the action that actually completes the DAW workflow. Love versus Keep has no concise
   on-screen contract.

7. **Generation latency lacks product treatment.** A fast run is designed around roughly a
   minute or more and a full run around several minutes, but the UI offers only a text status and
   Cancel. Closing the editor destroys its editor-owned job runner, so a long render is tied to
   the window's lifetime.

8. **The empty and failure states do not teach or recover.** The empty map explains where future
   candidates will appear but does not lead the user through the first successful result. Errors
   are compressed into a status line, and normal settings expose developer filesystem paths.

9. **The result intelligence is not translated into producer language.** The engine can select
   favorites, informative candidates, exploration, and a surprise, but the plugin shows ranks
   and decimal scores. The system knows why a result is present without communicating that reason
   musically.

10. **The workflow stops at the batch boundary.** There is no first-class `More like this`,
    `Use as source`, recent-run recovery, or iteration history. Yet branching from a promising
    mutation is the most natural extension of the product thesis.

### Browser reviewer findings

The browser reviewer is useful as an engineering and calibration surface, but it should not be a
model for the production plugin. It repeats the map and numbered navigation, exposes every score,
and stacks eight full audio players plus feedback controls into a long page. Keep it as an
internal/advanced reviewer, then share behavior and terminology with the plugin without sharing
its information density.

## Product simplification decisions

| Current capability | Decision | Producer-facing form |
| --- | --- | --- |
| Source file and capture | Keep and elevate | Large waveform source card with drop, record, play, replace |
| Two reference files and weights | Keep, simplify | One obvious Direction slot; additional references and blend live in an expandable area |
| Transform | Keep as the hero control | `Change`: Familiar / Evolved / Unrecognizable, with continuous control underneath |
| Groove, melody, timbre locks | Keep and reframe | `Preserve` chips with short, contextual explanations |
| Spread | Keep in the engine, remove from default setup | Automatically return a useful range; offer `Tighter` / `Wider next batch` after results or under Adjust |
| Drift | Remove from primary UI | `Avoid my library` appears only after a library has been indexed; detailed strength is advanced |
| Opinion | Remove from primary UI | Adaptive by default; optional preference influence in Preferences |
| Fast mode | Remove from creative UI | Product chooses an appropriate render profile; quality/time preference belongs in settings |
| Style hint | Demote | Optional `Add direction` text beneath the reference slot, not an always-open text box |
| Seed, oversampling, threshold, batch size | Keep internal | Advanced diagnostics only |
| Embedding map | Demote, do not delete | Optional `Map` result view after a batch exists |
| Candidate decimal scores | Hide by default | One human explanation such as `closest to your reference` or `widest departure`; raw data in inspector |
| Love / Keep / Discard / Undo | Simplify | Primary `Keep` and `Pass`; optional star means `Favorite`; standard undo and a brief toast |
| Drag WAV | Elevate | The selected waveform/card is directly draggable; a visible `Use in DAW` drag affordance records export |
| Pairwise calibration | Keep out of the core loop | Optional calibration task in Preferences, never an unsolicited blocking card |
| Taste event count/confidence | Remove from creative UI | Plain-language learning state and detailed evidence only in Preferences/diagnostics |
| Raw runtime paths | Remove from normal settings | First-run setup and health checks; paths remain under Advanced diagnostics |
| Eight results | Keep initially | Compact waveform cards optimized for rapid serial audition |

### Why Change remains and Range becomes contextual

The engine still needs both concepts:

- **Change:** How far may each result move from the source?
- **Range:** How different should the eight results be from one another?

Only Change is essential before a first batch. Diverge's promise already implies a deliberately
varied set, so it should choose a strong Range default. After hearing a batch, `Tighter next batch`
and `Wider next batch` are easy to understand because they answer an experienced problem. `Drift`
and `Opinion` describe how the selector calculates a result rather than an essential creative
brief. They should be automatic or conditional.

### Feedback contract

The labels need stable, visible meaning:

- **Keep:** I may use this; teach toward it.
- **Pass:** Not for me in this context; teach away from it.
- **Favorite (star):** A stronger positive signal, optional and visually secondary.
- **Use in DAW / drag:** Record an export signal and treat it as at least a Keep.

Undo should be a normal reversible action with `Cmd/Ctrl+Z` and a short-lived notification, not
a peer to the recurring decisions.

## Proposed experience

### 1. Prepare

The first screen should feel ready in under five seconds, even before a user understands every
feature.

```text
+--------------------------------------------------------------------+
| DIVERGE                                                     [gear] |
|                                                                    |
|  SOURCE                                                            |
|  [ Drop, record, or choose audio -- waveform -- play -- replace ]  |
|                                                                    |
|  DIRECTION                                                         |
|  [ Optional reference -- waveform -- play ]  [ + Add direction ]  |
|                                                                    |
|  CHANGE                              PRESERVE                        |
|  Familiar--------------Wild         [Groove] [Melody] [Timbre]    |
|                                                                    |
|                    [ Create 8 variations ]                          |
+--------------------------------------------------------------------+
```

Behavior:

- Dropping a file immediately draws a waveform and enables source playback.
- Recording shows input level, elapsed time, a 30-second limit, and an explicit Stop.
- The primary reference defaults to full influence. A second reference reveals a simple blend,
  not two unexplained decimal weights.
- Controls use descriptive endpoints and tooltips; exact values remain available to keyboard
  and accessibility users.
- Diversity uses a musically useful default. `Tighter` and `Wider` appear only under Adjust or as
  a suggestion after the producer hears a batch.
- The CTA states the outcome (`Create 8 variations`) rather than the implementation (`Generate`).
- The first run shows one sentence: `Audio stays on this Mac.`

### 2. Generating

The workspace should remain stable while the CTA becomes a progress surface.

- Show honest stages: Preparing, creating candidates, comparing, choosing eight, ready.
- Show completed/total work and a learned time range when available; do not invent a precise ETA.
- Keep the source and brief visible, with controls locked only when changing them would invalidate
  the active job.
- Let the editor close and reopen without cancelling the render. Job state belongs to the
  processor or a shared service, not the editor.
- Preserve a completed run if loading the UI fails.
- Put Cancel behind a lower-emphasis action and explain that completed prior runs are safe.
- Surface actionable recovery for missing runtime, models, disk space, invalid audio, and child
  process failure.

### 3. Explore and audition

Results replace the setup's visual emphasis without hiding the brief that produced them.

```text
+--------------------------------------------------------------------+
| < brief   8 variations                    [Grid] [Map] [Create new] |
|                                                                    |
| [1 waveform  play  star]  [2 waveform  play  star]                 |
| [3 waveform  play  star]  [4 waveform  play  star]                 |
| [5 waveform  play  star]  [6 waveform  play  star]                 |
| [7 waveform  play  star]  [8 waveform  play  star]                 |
|                                                                    |
| SELECTED: 03  "wide departure, groove preserved"                  |
| [Source A/B]  [Pass] [Keep]  [More like this]  [Drag to DAW >>>]  |
|                         [Adjust: Tighter / Wider next batch]       |
+--------------------------------------------------------------------+
```

Behavior:

- Clicking a candidate plays it immediately; selecting another stops and switches cleanly.
- Space toggles playback, arrows or J/K change candidate, A/B switches source and result,
  K keeps, X passes, and a visible shortcut hint is available without dominating the screen.
- Every candidate has a waveform, playback/progress state, selection state, and decision state.
- Only one result is expanded at a time. The rest remain dense and scannable.
- The selected result is draggable from its waveform or explicit DAW affordance.
- `More like this` uses the selected result as the next source, carries forward sensible preserve
  settings, and creates a visible branch in recent history.
- `Map` is a secondary view for spatial exploration. It shares selection and playback state with
  the grid; it never becomes a separate workflow.
- One special label such as `Wildcard` is useful. A taxonomy of internal selection roles is not.

### 4. Return and recover

- Reopening the plugin restores the active job or most recent batch for that instance.
- A small recent-runs drawer shows source thumbnail/waveform, date, brief, and kept result count.
- Missing moved files produce a repair flow rather than silently empty slots.
- Session state persists source, references, creative controls, selected candidate, decisions,
  and run identifier. Runtime paths are global settings, not repeated per DAW instance.

### 5. Preferences without administration

Normal use should require no Preferences screen. The optional panel can show:

- `Learns only from choices you make. Stored locally.`
- a simple influence control with Automatic as the default;
- how well Diverge knows the user's preferences in plain language;
- editable inferred tendencies, clearly marked as hypotheses;
- representative favorites and passes;
- pause learning, reset with recovery, import, export, and optional calibration.

Do not show training lifecycle, model version, observation counts, confidence percentages, or
prompt additions in the header. Those belong in diagnostics, if anywhere.

## Visual and interaction direction

### Character

The product should feel like a precise studio instrument with a sense of discovery, not an AI
dashboard. Use a near-black neutral foundation, restrained cool surfaces, one vivid exploration
accent, and one warm decision accent. Avoid sparkles, robot metaphors, generic neon gradients,
glass everywhere, and dense panels of glowing knobs.

The current navy/cyan palette is serviceable but generic. The redesign should earn identity
through waveform treatment, spatial traces, typography, and motion rather than decoration.

### System

- Establish a custom JUCE `LookAndFeel` and reusable tokens for color, spacing, radius, type,
  focus, hover, disabled state, and motion timing.
- Use a clear type hierarchy: product/title, section label, primary value, supporting text.
- Make waveforms the recurring visual motif across source, references, candidates, history,
  and preference examples.
- Use 8-point spacing, 44-pixel minimum primary targets, crisp Retina rendering, and purposeful
  density suited to plugin windows.
- Animate state changes in roughly 120-200 ms; use longer motion only for generation progress.
- Respect reduced motion and preserve full keyboard navigation and visible focus.
- Meet WCAG AA contrast where JUCE permits it and never communicate state only by color.
- Design and test at the existing minimum size first, then make additional width improve the
  candidate grid rather than merely enlarging an empty canvas.

### Micro-interactions worth polishing

- File drop acceptance and rejection before mouse-up.
- Immediate waveform placeholder followed by resolved waveform.
- Seamless play/stop/switch behavior with a clear playhead.
- Source/result A/B without gain jumps.
- Decision state stamped on a card with one-click undo.
- Drag lift, cursor, and successful handoff feedback.
- Generation completion that focuses the first candidate and makes the next action obvious.

## Architecture required by the experience

This is not only a `PluginEditor.cpp` reskin. The following product requirements need structural
work:

1. **Move job ownership out of the editor.** A render must survive editor closure. The processor
   or a reference-counted service should own job state, progress, cancellation, and result loading.

2. **Create an explicit UI state model.** At minimum: needs setup, ready, generating, loading,
   results, cancelled, recoverable error, and fatal setup error. Components should render from
   state rather than mutating unrelated labels ad hoc.

3. **Persist instance workflow state.** Store creative control values, file references, run ID,
   selected result, and decisions through plugin state. Validate external files when restoring.

4. **Add waveform and transport primitives.** Cache thumbnails, expose source/reference/result
   preview state, support seek and A/B, and normalize audition levels safely.

5. **Separate global setup from creative state.** Runtime, models, library, output policy, and
   diagnostics should be global preferences with health checks. A distributable build should not
   depend on a development checkout path.

6. **Use a typed result model.** Parse manifest data once into candidate objects with paths,
   roles, decisions, explanations, and raw diagnostics. Do not assemble user-facing prose from
   JSON inside selection callbacks.

7. **Make decisions durable and responsive.** Update UI optimistically, append events through a
   serialized service, train asynchronously, expose retry on failure, and never lose rapid input.

8. **Support branch history.** Record the parent run/candidate when a result becomes a source.
   Preserve existing bundle compatibility by making lineage additive.

9. **Improve job protocol.** Emit structured stage/progress/error events rather than parsing only
   `PROGRESS` text. Keep human-readable logs for diagnostics.

10. **Design for automated UI states.** Add deterministic mock runs and injectable services so
    every state can be rendered and tested without models, MPS, or a multi-minute render.

## Delivery plan

Each phase is a cohesive, testable increment. Do not wait until the final phase for host testing.

### Phase 0 — validate the brief and baseline

- Conduct five observed producer sessions with the current standalone/plugin flow.
- Record time to first valid generation, control comprehension, audition path, outputs used, and
  points where users ask what a term means.
- Test the proposed Change/Preserve language and contextual Tighter/Wider actions with concrete
  audio examples.
- Confirm the exact Keep/Pass/Favorite contract and whether automatic playback is welcome.
- Capture screenshots and recordings at minimum, default, and large plugin sizes.

Gate: at least four of five producers can accurately predict the effect of Change and Preserve,
then understand Tighter/Wider after hearing one batch, without implementation terminology.

### Phase 1 — product and UI foundations

- Introduce design tokens, custom LookAndFeel, icons, typography, and reusable controls.
- Add the explicit UI state model and typed source/reference/run/candidate models.
- Move generation and decision services out of editor lifetime.
- Persist instance workflow state and implement active-run restoration.
- Add deterministic UI fixtures for empty, ready, generating, result, and error states.

Gate: closing and reopening the editor during a mock or real generation preserves the job and
correct state; no child process is orphaned on plugin teardown.

### Phase 2 — rebuild Prepare

- Implement source and direction waveform cards with drop, choose, capture, audition, and replace.
- Replace primary knobs with Change and Preserve; make Range a contextual Tighter/Wider
  adjustment after a batch.
- Make library avoidance conditional; move preference influence, render profile, additional references,
  and text direction to progressive disclosure.
- Add first-run runtime/model health checks and actionable setup errors.
- Replace raw settings with Studio, Library, Storage, Preferences, and Advanced sections.

Gate: a new user can reach a valid generation without opening settings or encountering a raw path.

### Phase 3 — rebuild generation and results

- Implement structured progress stages, resilient cancellation, and recoverable errors.
- Add compact waveform candidate cards, unified selection/playback, seek, source A/B, and shortcuts.
- Implement Keep, Pass, Favorite, standard undo, durable decision state, and clear semantics.
- Make selected audio directly draggable and record export decisions.
- Translate manifest reasons into short producer-facing explanations.

Gate: a producer can audition all eight, classify them, and drag one into a DAW without consulting
documentation or scrolling the plugin window.

### Phase 4 — deepen the core loop

- Add More like this / Use as source with additive run lineage.
- Add recent-run recovery and kept-result filtering.
- Reintroduce the map as a synchronized secondary result view.
- Build the optional Preferences panel and move calibration/pairwise work there.
- Align browser-review terminology and feedback semantics while retaining it as an advanced tool.

Gate: a producer can branch twice from a result, navigate the lineage, and recover every kept or
exported file after reopening the session.

### Phase 5 — polish and production hardening

- Tune layout, motion, hover/focus, waveform rendering, loading transitions, and copy.
- Validate keyboard-only use, contrast, reduced motion, Retina, and all supported window sizes.
- Exercise missing models, invalid files, moved paths, full disk, process crash, cancellation,
  DAW sample-rate changes, and rapid decision input.
- Validate Standalone, Logic AU, Ableton VST3, plugin reload, project save/restore, and `auval`.
- Package a relocatable runtime or a polished first-run locator before distribution.

Gate: no P0/P1 usability failures in a fresh five-producer validation pass, and the full automated
and host validation matrix passes.

## Success measures

Diverge is local-first and intentionally has no telemetry. Measure these in opt-in usability
sessions and deterministic QA runs, not through hidden analytics.

- Median interaction time from opening the plugin to starting a valid first generation: under
  30 seconds, excluding source discovery.
- From generation completion to first audition: one action.
- Audition and disposition of eight results: under 90 seconds for an eight-second source.
- At least 80% of test users correctly explain Change and Preserve after one use and understand
  Tighter/Wider after hearing one batch.
- At least 60% of observed batches produce a Keep, Favorite, export, or deliberate branch during
  thesis validation; track the number as a learning signal, not a launch claim.
- No normal workflow exposes Python paths, model files, selection weights, or raw decimal scores.
- Closing/reopening the editor loses no active or completed generation work.
- Every visible control has an effect in the current configuration; conditional features are
  hidden or clearly disabled with a reason.
- Minimum-size and keyboard-only workflows remain complete.
- No audio, taste data, or behavior leaves the machine.

## Explicitly out of scope for this overhaul

- Cloud rendering, accounts, credits, sharing, or telemetry.
- Real-time diffusion or generation on the audio thread.
- A text-prompt-first mode or full-song generation.
- Fine-tuning, LoRA, stems, Windows, or AAX.
- A large preset marketplace, social feed, or community taste model.
- Exposing every model and selection parameter in the name of power-user flexibility.
- Replacing the proven generation and selection pipeline before the redesigned workflow has been
  tested with producers.

## Immediate next increment

Start with Phase 0 and the Phase 1 state/lifecycle foundation, not a visual rewrite in the current
editor architecture. In parallel with producer sessions, build a deterministic UI harness that can
show every plugin state instantly. Use that harness to iterate on the Prepare and Results layouts
with real waveforms and realistic copy before wiring them to multi-minute generation.

The first implementation PR should therefore be narrowly scoped to:

1. typed plugin workflow state and candidate models;
2. processor-owned generation lifecycle that survives editor closure;
3. persisted run/source/control state;
4. mock fixtures for every major UI state;
5. no intentional visual redesign beyond what is required to expose and test those states.

That foundation makes the aesthetic overhaul safe, testable, and genuinely fluid rather than a
beautiful editor that still loses work or reflects process state inconsistently.
