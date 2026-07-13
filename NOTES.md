# Implementation notes

- CLAP uses Transformers `laion/clap-htsat-unfused`, the spec-approved fallback. This avoids
  `laion_clap` packaging friction on Apple Silicon and is used consistently throughout.
  Project audio remains 44.1 kHz; the wrapper resamples a private inference copy to CLAP's
  required 48 kHz input rate.
- Transform calibration is currently the monotonic mapping `0..100 -> 0.10..1.00`. Values
  10 and 90 therefore map to 0.19 and 0.91. Human listening calibration is required at the
  milestone-4 STOP before this mapping is considered perceptually calibrated.
- Spread maps quadratically from `0..100 -> 0..1.5`, giving finer control near zero while
  retaining strong farthest-point pressure at the top. A linear map made spread 10 dominate
  small utility differences and selected seven of the same eight files as spread 90 in the
  first real calibration pool. Human listening confirmation is still required at the
  milestone-4 STOP.
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
- On the development Apple Silicon Mac, the full 32-to-8, 8-second `--fast` run measured
  5:03.56. This misses the spec's `<90 s` fast target and narrowly misses the five-minute
  default ceiling. The implementation preserves the required independent `seed+i` loop;
  opt-in microbatching is the clearest future speed path but would need a batched seeded-noise
  adapter and peak-memory testing to preserve reproducibility.
