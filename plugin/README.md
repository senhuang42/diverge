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
3. Select **Create up to 8 variations**. The job continues if the editor closes.
4. Click waveform cards to switch audition instantly; previews are resampled to the active host
   rate and loudness-matched to the source for audition only. Source A/B preserves the musical
   position and replaces rather than layers over live input.
5. Drag a selected card or choose **Use in DAW**. Diverge first retains a content-addressed object
   and hands the DAW a stable named export under the user's application-support library. Branch
   sources are retained there too, with append-only provenance events. Branch with **More like
   this**, recover a prior batch from **Recent**, or inspect the synchronized **Map** view.

Quality and Preserve checks are hard gates. If fewer than eight results pass, the grid shows only
the valid subset. **Try more** runs another pool with the same constraints; it does not relax them.
Mono and stereo tracks remain mono and stereo when captured; captures use the active host sample
rate and are capped at 30 seconds.

Keep, Pass, Favorite, Use in DAW, and Branch are independent candidate choices: recording one does
not erase another. Choice changes are appended to each run's `decision-events.jsonl`; the current
snapshot is stored in the versioned `decisions.json` sidecar. Older single-decision sidecars and
saved plugin states migrate when restored. Creative state, the active run, and selection are also
restored with the plugin instance. Rapid taste updates are queued and learned locally. Closing only
the editor preserves an active job; destroying the plugin instance cleanly cancels its child process.

For deterministic UI review without running models, set `DIVERGE_UI_FIXTURE` to `empty`,
`ready`, `generating`, `results`, `recent`, `map`, `settings`, or `error` before opening the
Standalone build. Set `DIVERGE_UI_SNAPSHOT=/path/to/out.png` to write a PNG of the editor
about a second after launch and quit, and `DIVERGE_REDUCED_MOTION=1` to disable animation.
