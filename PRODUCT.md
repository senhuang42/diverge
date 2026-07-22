# Product

<!-- impeccable:product-schema 1 -->

## Platform

desktop-plugin

Diverge ships as a JUCE 8 audio plugin in AU, VST3, and Standalone form, targeting Apple Silicon
macOS. This is none of the schema's four values: no iOS or Android platform guidance applies, and
the mechanical web detector cannot scan its C++ sources, so the craft floor is verified by
rendered snapshots rather than by a static scan.

## Users

An electronic producer, beatmaker, media composer, or sound designer working on Apple Silicon in
Logic Pro or Ableton Live. They already have a one-shot, texture, recording, or 1-8 bar fragment
worth keeping, but it is too literal, familiar, or incomplete. They are mid-session with momentum
on the line, working in a dim room against a dark DAW, and they want surprise without giving up
authorship or leaving the host.

## Product Purpose

When a promising sound is stuck, help the producer explore controlled alternatives and place a
useful result back into the session before momentum is lost. Success is a result dragged into the
session, not time spent in the plugin.

Product promise: keep what matters, change what does not, hear the useful possibilities.

## Positioning

A controlled variation instrument that turns a producer's own session fragment into a small,
meaningfully different set while preserving the musical properties they choose. The defensible
mechanism is the creative contract, not model access: the producer states what must survive
(Groove, Melody, Timbre) and how far the batch may move (Change), and the batch is oversampled,
validated against that contract, and selected for non-redundancy before it is ever shown.

Diverge is not a prompt-to-song service, sample marketplace, autonomous composer, real-time audio
effect, or replacement for detailed synthesis. It is pull-based and generate-then-curate.

## Operating Context

The plugin runs inside a host session. Audio passes through except while auditioning, when the
source or result replaces the live input for a true A/B at matched loudness and preserved musical
position. Capture arms to the next host bar at a 1, 2, 4, or 8-bar length. All model work runs
locally in a configured Python environment on a background child process; nothing leaves the Mac.
Jobs survive the editor closing. Results are chosen, marked, and either dragged to the DAW or
branched from.

## Capabilities and Constraints

- Every batch presents exactly eight results. Quality and Preserve checks are hard gates; missing
  slots are filled with labeled, source-derived lock-safe treatments rather than by lowering the
  requested threshold.
- Keep, Pass, Favorite, Use in DAW, and Branch are independent per-candidate choices; recording
  one does not erase another. Choices are appended to a per-run event log and learned locally.
- Creative state, active run, and selection restore with the plugin instance.
- Deterministic UI fixtures (`DIVERGE_UI_FIXTURE`) and a snapshot hook (`DIVERGE_UI_SNAPSHOT`)
  exist for review without running models; `DIVERGE_REDUCED_MOTION` disables animation.
- The experimental variation map has no stable musical semantics and stays out of the default
  workflow.

## Brand Commitments

The name "Diverge" is retained. As of this overhaul the user confirmed that the previous mint/teal
accent and the three-stroke diverging brand mark are **not** binding and may be replaced; no
palette, typeface, or mark is currently a commitment.

## Evidence on Hand

- Working local pipeline, models, and prior runs in this checkout (`models/`, `runs/`,
  `choices.jsonl`).
- Research and competitive assessment in `PLUGIN_PRODUCT_SPEC.md` (July 21, 2026).
- Deterministic UI fixtures covering empty, ready, generating, results, brief-results, recent,
  settings, and error states.
- No customer, benchmark, pricing, or licensing claims exist. Future work must not fabricate them.

## Product Principles

1. The producer's own material is the subject; the plugin never starts from a blank page.
2. The creative contract is honored literally. Preserve locks and Change are promises, not hints.
3. Always show a full, non-redundant set. Never silently relax what was asked for.
4. Choosing is the real work. Comparison, marking, and recovery outrank generation chrome.
5. Everything stays local, and the interface should make that legible rather than merely claimed.

## Accessibility & Inclusion

State must never be carried by color alone; decisions and locks also read through shape, mark, or
text. Keyboard operation covers selection, audition, and the Keep/Pass choices. Reduced motion is
honored through `DIVERGE_REDUCED_MOTION`.
