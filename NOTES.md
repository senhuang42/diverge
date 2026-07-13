# Implementation notes

- CLAP uses Transformers `laion/clap-htsat-unfused`, the spec-approved fallback. This avoids
  `laion_clap` packaging friction on Apple Silicon and is used consistently throughout.
  Project audio remains 44.1 kHz; the wrapper resamples a private inference copy to CLAP's
  required 48 kHz input rate.
- Transform uses the monotonic mapping `0..100 -> 0.10..1.00`. Values 10 and 90 therefore
  map to 0.19 and 0.91. Human listening on real CC0 drum/guitar material accepted the
  transform behavior after short sources were loop-filled instead of zero-padded.
- Spread maps quadratically from `0..100 -> 0..1.5`, giving finer control near zero while
  retaining strong farthest-point pressure at the top. A linear map made spread 10 dominate
  small utility differences and selected seven of the same eight files as spread 90 in the
  first real calibration pool. Human listening accepted the corrected spread-90 batch as
  materially more varied than spread 10.
- Stable Audio model code is imported lazily. The loader uses the local checkpoint written by
  the download script and never permits an inference-time model download.
  Stable Audio's T5 Base text conditioner is downloaded by the same script and its config is
  rewritten in memory to the local path before model construction; Hugging Face and
  Transformers offline modes are enabled during inference. `stable-audio-tools==0.0.19`
  asserts that the T5 identifier belongs to a fixed remote-name allowlist, so the adapter
  registers the local path with the library's T5 dimension table before constructing the
  unchanged T5 Base conditioner.
- `stable-audio-tools==0.0.19` pins NumPy 1.23.5, so SciPy/scikit-learn/librosa/UMAP are
  pinned to the newest mutually compatible Python 3.11 releases in `uv.lock`.
- The real extra pins setuptools 80.9.0 because stable-audio-tools' transitive OpenAI CLIP
  dependency still imports `pkg_resources`, removed from newer setuptools releases.
- MPS inference uses fp32 first for reliability. If the installed stable-audio-tools API
  differs, the adapter fails with an actionable version/API error instead of silently using
  another model.
- The real sampler constructs deterministic per-item `seed+i` noise and denoises eight
  candidates per MPS microbatch. Latent decoding is split into pairs because decoding a
  large batch causes severe MPS memory pressure. Source onset/chroma features are cached
  across candidate lock scoring. On the development Apple Silicon Mac, the exact default
  32-to-8, 8-second acceptance run measured 3:04.13 (under the five-minute gate). `--fast`
  uses four steps and a 16-candidate pool by default and measured 87.01 seconds (under the
  90-second gate); an explicit `--n-oversample` overrides the fast pool size.
- Initial real calibration accidentally used generic Stable Audio settings (`cfg_scale=6`,
  Euler) instead of Open Small's published 8-step settings (`cfg_scale=1`, `pingpong`). It
  also let stable-audio-tools zero-pad a 2-second source to 8 seconds and used the meaningless
  filename prompt `ref a`. The real adapter now follows the model card, loop-fills short source
  audio, and accepts an explicit `style_text_hint`. `--fast` remains accepted but Open Small's
  official eight-step path is already its optimized inference path.
- JUCE is pinned to 8.0.9 with CMake FetchContent. The local Apple Silicon build produces
  AU, VST3, and Standalone artifacts with ad-hoc signatures; `auval -v aufx Dvge Snhg`
  passes. Distribution still requires an Apple Developer ID Application certificate,
  hardened runtime signing, `xcrun notarytool submit`, and `xcrun stapler staple` for each
  packaged artifact. Those credentials are intentionally outside the repo.
- The plugin compiles the current checkout path as a development default for Python/models/
  runs, while persisting user overrides in plugin state. A redistributed build should bundle
  a relocatable runtime or add a first-run locator instead of relying on that development
  path.
- The choices history is also an explicit persisted plugin/config path. This avoids silently
  losing taste and anti-repetition behavior when a DAW launches with an unrelated working
  directory.
