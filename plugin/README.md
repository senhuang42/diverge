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

The default settings point at this checkout's `.venv`, `models/`, `choices.jsonl`, and `runs/` paths. Open
the in-plugin Settings panel after moving the checkout or Python environment.
For a host-free smoke test, open
`plugin/build/DivergePlugin_artefacts/Release/Standalone/Diverge.app`.

## Host workflow

1. Drop or choose a source; optionally add two weighted references.
2. Choose locks and Transform/Spread/Drift. Fast mode is on by default.
3. Generate. The UI remains responsive and reports `PROGRESS i/N`.
4. Click a map point or numbered candidate, audition it, then Keep/Discard.
5. Drag the selected WAV into the DAW.

The taste critic retrains on plugin launch and after every ten completed decisions. Closing
the editor or plugin cancels its active Python child before teardown.
