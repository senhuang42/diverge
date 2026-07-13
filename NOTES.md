# Implementation notes

- CLAP uses Transformers `laion/clap-htsat-unfused`, the spec-approved fallback. This avoids
  `laion_clap` packaging friction on Apple Silicon and is used consistently throughout.
  Project audio remains 44.1 kHz; the wrapper resamples a private inference copy to CLAP's
  required 48 kHz input rate.
- Transform calibration is currently the monotonic mapping `0..100 -> 0.10..1.00`. Values
  10 and 90 therefore map to 0.19 and 0.91. Human listening calibration is required at the
  milestone-4 STOP before this mapping is considered perceptually calibrated.
- Spread maps linearly from `0..100 -> 0..1.5`. Human listening calibration is likewise
  required at the milestone-4 STOP.
- Stable Audio model code is imported lazily. The loader uses the local checkpoint written by
  the download script and never permits an inference-time model download.
- `stable-audio-tools==0.0.19` pins NumPy 1.23.5, so SciPy/scikit-learn/librosa/UMAP are
  pinned to the newest mutually compatible Python 3.11 releases in `uv.lock`.
- The real extra pins setuptools 80.9.0 because stable-audio-tools' transitive OpenAI CLIP
  dependency still imports `pkg_resources`, removed from newer setuptools releases.
- MPS inference uses fp32 first for reliability. If the installed stable-audio-tools API
  differs, the adapter fails with an actionable version/API error instead of silently using
  another model.
