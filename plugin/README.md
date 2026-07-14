# Diverge plugin

JUCE 8 shell for the local Diverge pipeline. The plugin passes host audio through and runs
all model work in the configured Python environment on a background child process. AU,
VST3, and Standalone use the same source.

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

The default setup points at this checkout's `.venv`, `models/`, `choices.jsonl`, and `runs/`
paths. Normal use does not expose them; after moving the checkout or Python environment,
open **Settings → Advanced diagnostics** to repair the local engine or storage location.
For a host-free smoke test, open
`plugin/build/DivergePlugin_artefacts/Release/Standalone/Diverge.app`.

## Host workflow

1. Drop, record, or choose a source; optionally add a reference or short direction.
2. Set **Change**, then choose which of Groove, Melody, and Timbre to preserve.
3. Select **Create 8 variations**. Honest stages and completed work remain visible, and the
   processor-owned job continues if the editor closes.
4. Click waveform cards to switch audition instantly; use Source A/B, Keep, Pass, Favorite,
   or keyboard shortcuts without leaving the result grid.
5. Drag a selected card or choose **Use in DAW**. Branch with **More like this**, recover a
   prior batch from **Recent**, or inspect the synchronized **Map** view.

Creative state, the active run, selection, and decision sidecars are restored with the plugin
instance. Rapid decisions are queued and learned locally. Closing only the editor preserves
an active job; destroying the plugin instance cleanly cancels its child process.

For deterministic UI review without running models, set `DIVERGE_UI_FIXTURE` to `empty`,
`ready`, `generating`, `results`, `recent`, or `error` before opening the Standalone build.
