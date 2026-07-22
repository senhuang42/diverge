# Diverge plugin

JUCE 8 shell for the local Diverge pipeline. The plugin passes host audio through except while
auditioning, when the source or result replaces the live input for a true A/B. All model work runs
in the configured Python environment on a background child process. AU, VST3, and Standalone use
the same source.

## Build

```bash
cmake -S plugin -B plugin/build -G Xcode
cmake --build plugin/build --config Release \
  --target DivergePlugin_AU DivergePlugin_VST3 DivergePlugin_Standalone -j 4
```

Artifacts are under `plugin/build/DivergePlugin_artefacts/Release/`. Builds are ad-hoc
signed for local use. Install the formats for the current user with:

```bash
ditto plugin/build/DivergePlugin_artefacts/Release/AU/Diverge.component \
  "$HOME/Library/Audio/Plug-Ins/Components/Diverge.component"
ditto plugin/build/DivergePlugin_artefacts/Release/VST3/Diverge.vst3 \
  "$HOME/Library/Audio/Plug-Ins/VST3/Diverge.vst3"
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

The locally built AU validates with:

```bash
auval -v aufx Dvge Snhg
```

Run the deterministic helper, preview, asset, workflow, and host-audio contract tests with:

```bash
ctest --test-dir plugin/build -C Debug --output-on-failure
```

The default setup points at this checkout's `.venv`, `models/`, `choices.jsonl`, and `runs/`
paths. Normal use does not expose them; after moving the checkout or Python environment,
open **Settings → Advanced diagnostics** to repair the local engine or storage location.
For a host-free smoke test, open
`plugin/build/DivergePlugin_artefacts/Release/Standalone/Diverge.app`.

## Host workflow

1. Drop, record, or choose a source; optionally add a reference or short direction.
2. Set **Change**, then choose which of Groove, Melody, and Timbre to preserve.
3. Select **Create 8 variations**. The job continues if the editor closes.
4. Click waveform cards to switch audition instantly; previews are resampled to the active host
   rate and loudness-matched to the source for audition only. Source A/B preserves the musical
   position and replaces rather than layers over live input. The first audition waits for the next
   host beat when transport facts are available; switches remain immediate and position-matched.
   A card explains a measured, batch-distinct difference when the evidence is strong enough;
   otherwise it stays quiet.
5. Drag a selected card or choose **Use in DAW**. Diverge first retains a content-addressed object
   and hands the DAW a stable named export under the user's application-support library. Branch
   sources are retained there too, with append-only provenance events. Branch with **More like
   this** or recover a prior batch from **Recent**. Recent restores the saved source, reference,
   direction, Change, Preserve settings, and choices.

Quality and Preserve checks are hard gates. The plugin generates a fast model pool, validates it,
then fills any missing slots with labeled, source-derived lock-safe treatments. It always presents
eight results without silently lowering the requested Preserve threshold.
High Change with all three Preserve locks shows a warning but remains available to try.
Choose a 1, 2, 4, or 8-bar capture. In a host, recording arms until the next bar and then stops at
the requested length. Mono/stereo layout, sample rate, tempo, and time signature are preserved as
host facts in the run config. Host-free capture starts immediately with a 120 BPM fallback.

Keep, Pass, Favorite, Use in DAW, and Branch are independent candidate choices: recording one does
not erase another. Choice changes are appended to each run's `decision-events.jsonl`; the current
snapshot is stored in the versioned `decisions.json` sidecar. Older single-decision sidecars and
saved plugin states migrate when restored. Creative state, the active run, and selection are also
restored with the plugin instance. Rapid taste updates are queued and learned locally. Closing only
the editor preserves an active job; destroying the plugin instance cleanly cancels its child process.

For deterministic UI review without running models, set `DIVERGE_UI_FIXTURE` to `empty`,
`ready`, `generating`, `results`, `brief-results`, `recent`, `settings`, or `error` before opening
the Standalone build. The experimental `map` fixture remains available for comparative testing. Set
`DIVERGE_UI_SNAPSHOT=/path/to/out.png` to write a PNG about a second after launch and quit, and
`DIVERGE_REDUCED_MOTION=1` to disable animation.
