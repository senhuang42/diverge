# Design

<!-- impeccable:design-schema 1 -->

Durable visual decisions for the Diverge plugin surface. Product truth lives in `PRODUCT.md`.

## Direction contract

**THESIS.** Diverge is a photographic contact sheet: eight takes of one idea, laid out to be
scanned, compared, and marked. It refuses the arrangement this category always ships — near-black
ground, one neon accent, glowing edges on every raised element, and a tracked uppercase eyebrow
over each section.

**OWN-WORLD.** Warm darkroom graphite ground at very low chroma around hue 40, so the neutrals
read as board and paper stock rather than as cool screen grey. Frames are flat cells separated by
rules, never by shadow. Chinagraph red is the grease pencil and appears nowhere except where a
person made a decision. Safelight amber reports machine state. Focus is not a colour: the frame
under the loupe lifts toward paper white. Recognisable with all content removed by the mark
vocabulary alone — a circled take, a struck take, an arrow off the sheet.

**STORY.** The producer arrives with material, states a contract, and receives eight takes. They
scan, audition against the source, mark keepers and rejects, and pull one into the session.

**FIRST VIEWPORT.** Brief screen: the source waveform dominates as the subject, the contract sits
beneath it as two grouped decisions, and one filled chinagraph action closes the screen. Results
screen: the eight frames own the full field, with the selected take's detail and the single
primary action anchored at the foot.

**FORM.** Contact sheet, ranked first on the grounded list; user-pinned over concept-seed roll
`609828e3`, which assigned index 6 (modular synth panel). Grounds for the override were recorded
as a task-fit concern and then settled by the user directly.

## Visitor mode

**Operate.** The producer is completing a task with session momentum on the line. Scanability,
stable density, and native affordances outrank expression. The world lives in precise details —
the marks, the rules, the type ramp — not in decoration that competes with the waveforms.

## Colour

Colour is spent on exactly two jobs, which is what gives two accents their force on a surface
this dark.

| Role | Value | Job |
|---|---|---|
| `canvas` | `#151310` | Deepest ground |
| `canvasHi` | `#1b1814` | Ground, lit side |
| `surface` | `#201d18` | The sheet frames sit on |
| `raised` | `#2a2620` | A frame cell |
| `hairline` | `#322d25` | Quiet rule |
| `edge` | `#443d32` | Stated rule |
| `text` | `#f4f0e7` | Photographic paper white |
| `muted` | `#a79d8b` | Secondary text |
| `dim` | `#6e6656` | Tertiary text, idle marks |
| `exploration` | `#ff4a26` | Chinagraph: a person decided here |
| `decision` | `#f0a93c` | Safelight: the machine is working or wants attention |

Rules that bind:

- Chinagraph red never marks machine state, and safelight amber never marks a human choice.
- Selection and focus use paper white and a lifted ground, never an accent hue.
- Neutrals stay warm. A cool grey anywhere on the surface is a defect.
- Secondary text on a coloured surface derives from that surface's hue, never generic grey.
- Body and placeholder text hold at least 4.5:1; large text and controls at least 3:1. Filled
  chinagraph buttons take the dark ground colour as their label, which clears 4.5:1 where paper
  white would not.

## Type

One workhorse grotesque (Helvetica Neue, falling back through Helvetica and Arial) carries the
whole surface. Monospace is reserved for what it is for: take numbers, timings, and filesystem
paths. Mono as a costume for "technical" is a defect.

| Role | Size | Weight | Tracking |
|---|---|---|---|
| display | 26 | Bold | -0.02em |
| title | 17 | Bold | -0.01em |
| lead | 15 | Regular | 0 |
| body | 13.5 | Regular / Bold | 0 |
| meta | 12 | Regular | 0 |
| caps | 10.5 | Bold | +0.12em |

Tracking stops at -0.04em and in practice stops at -0.02em. Tracked caps are a named kicker, not
section grammar: they are permitted on the transport legend and take badges, and nowhere else.
Section headings are sentence case at title weight, and grouping is carried by proximity.

## Depth and shape

Elevation is declared **once** per element, as a border or as a shadow, never both. A 1px border
under a wide soft shadow is the ghost card and is a defect.

- Frames and inline controls: a rule, no shadow.
- Panels, drawers, toasts, and the filled primary action: a shadow with a real offset and blur,
  no border.
- Zero-offset coloured halos are banned outright. There is no glow anywhere on this surface.
- Panel and frame radius is 13px. Pills are for small controls only; buttons use 9px.

## Space

Four-unit base scale. Tight groups, generous separation, and more space above a heading than
below it. Rhythm comes from the contrast between tight and generous intervals, not from repeating
one value until every element carries equal weight.

## Marks and state

Every state that matters reads through shape as well as colour, so it survives without colour
vision:

- **Kept** — a single circled ring on the frame.
- **Favourite** — a doubled ring.
- **Exported** — a doubled ring with an arrow leaving the sheet.
- **Passed** — the frame struck corner to corner and stepped back to 40% ink, never removed.

## Motion

One authored moment: the frames develop onto the sheet in a staggered entrance when a batch
arrives. Everything else is state feedback measured in a single ease toward its target. No
section-wide fades, no scattered hover effects, no animation added to make polish visible.
`DIVERGE_REDUCED_MOTION=1` resolves every animation instantly to its target.

## Verification

The mechanical detector cannot read C++, so the craft floor is verified by rendered snapshot.
Build the Standalone target and capture each fixture:

```bash
DIVERGE_UI_FIXTURE=<empty|ready|generating|results|brief-results|recent|settings|error> \
DIVERGE_REDUCED_MOTION=1 DIVERGE_UI_SNAPSHOT=/path/out.png \
plugin/build/DivergePlugin_artefacts/Release/Standalone/Diverge.app/Contents/MacOS/Diverge
```

Every state is reviewed as a render before it ships. A clean build is not evidence of craft.
