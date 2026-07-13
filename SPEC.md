# Diverge — v1 implementation spec

A local-first generative sampler for producers. Drop in source audio (a hummed idea, a rough
loop), steer with reference tracks instead of text, and get back a batch of 8 candidates that
are deliberately spread apart in aesthetic space — biased toward the user's taste and away
from generic/"crowd" sound. Keep/discard decisions train a local taste critic over time.

This spec is written for an autonomous coding agent (Claude Code / Codex) starting in an
EMPTY folder on an Apple Silicon Mac. Build phases strictly in order. Do not start a phase
until the previous phase's acceptance criteria pass.

---

## 0. Ground rules for the agent

- Target machine: Apple Silicon macOS 14+, Python 3.11, PyTorch with MPS. No CUDA anywhere.
- Everything runs locally. No network calls at runtime except the one-time model download
  script. Never upload user audio anywhere.
- Model weights are NEVER committed to the repo. Provide `scripts/download_models.py` and
  gitignore the `models/` directory.
- Every module must be importable and unit-testable WITHOUT model weights or GPU. Achieve
  this with a `MockGenerator` (see 3.4) and dependency injection. CI-style test run
  (`pytest -m "not slow"`) must pass on a machine with no weights downloaded.
- Prefer boring code: plain functions, dataclasses, type hints, no framework magic.
- Pin dependencies. Diffusion audio stacks break on point releases. Use a lockfile
  (`uv` preferred; `pip-tools` acceptable).
- When a library API doesn't match this spec exactly (likely — these libraries move fast),
  read the installed library source, adapt, and note the deviation in `NOTES.md`. Do not
  silently substitute a different model or approach.
- All audio I/O: 44.1 kHz, stereo, 32-bit float WAV. Resample inputs on load.
- Determinism: every generation takes an integer `seed`; identical seed + params must
  reproduce identical output (within MPS nondeterminism limits).

## 1. Repo layout

```
diverge/
  pyproject.toml
  README.md
  NOTES.md                  # agent's running log of deviations/gotchas
  scripts/
    download_models.py      # pulls Stable Audio Open Small + CLAP checkpoints
    build_library_index.py  # embeds the user's sample library
  src/diverge/
    __init__.py
    config.py               # dataclasses for all knobs; JSON (de)serialization
    audio_io.py             # load/save/resample/normalize
    features.py             # onset envelope, chroma, embeddings
    embed.py                # CLAP wrapper (audio -> 512-d vector), batch + cache
    generator.py            # GeneratorProtocol, StableAudioGenerator, MockGenerator
    locks.py                # per-lock similarity scores between candidate and source
    select.py               # oversample -> constrained farthest-point selection
    critic.py               # taste critic: train/score from choices.jsonl
    novelty.py              # distance-from-corpus scoring against library index
    map2d.py                # UMAP projection of a batch for the map view
    session.py              # orchestrates one full run; writes output bundle
    cli.py                  # `diverge run ...`, `diverge index ...`, `diverge critic ...`
  review/
    app.py                  # gradio review UI: players + 2D map + keep/discard
  plugin/                   # Phase 2 (JUCE). Empty until Phase 1 accepted.
  tests/
  data/                     # tiny test fixtures (<=3 s clips, committed)
  models/                   # gitignored
  runs/                     # gitignored; output bundles
  choices.jsonl             # gitignored; taste critic training data
```

## 2. Dependencies

- `torch` (MPS build), `torchaudio`
- `stable-audio-tools` (Stability AI) — generation
- `laion_clap` — audio embeddings (fallback: `transformers` CLAP `laion/clap-htsat-unfused`
  if `laion_clap` install fights on macOS; pick one, note it, use it everywhere)
- `librosa` — onset strength, chroma
- `soundfile`, `numpy`, `scipy`, `scikit-learn` (logistic regression, NearestNeighbors)
- `umap-learn`
- `gradio` — review UI
- `pytest`, `ruff`

MPS gotchas to handle up front: set `PYTORCH_ENABLE_MPS_FALLBACK=1` in the CLI entrypoint;
run diffusion in fp32 if fp16 produces NaNs on MPS (detect and fall back automatically);
keep generation length <= 30 s.

## 3. Phase 1 — the pipeline (this phase is the thesis test; everything else is chrome)

### 3.1 Config (`config.py`)

```python
@dataclass
class RunConfig:
    source: Path                    # required: user's input audio
    references: list[tuple[Path, float]]  # (path, weight 0..1); weights normalized
    transform: int = 45             # 0..100 -> init_noise mapping, see 3.4
    spread: int = 60                # 0..100 -> selection dispersion pressure
    drift: int = 35                 # 0..100 -> anti-crowd pressure in selection
    locks: set[str] = field(default_factory=lambda: {"groove"})  # subset of {groove, melody, timbre}
    n_return: int = 8
    n_oversample: int = 32          # candidates generated before selection
    duration_s: float = 8.0
    seed: int = 0
    library_index: Path | None = None   # from build_library_index.py
    critic_model: Path | None = None    # trained critic, optional
```

### 3.2 Features (`features.py`)

- `onset_envelope(y, sr) -> np.ndarray` — librosa onset strength, normalized, fixed hop.
- `chroma(y, sr) -> np.ndarray` — CQT chroma, beat-agnostic (frame-level).
- `groove_similarity(a, b) -> float` — max cross-correlation of onset envelopes over a
  small lag window (±120 ms), so slightly shifted grooves still count as similar. Range 0..1.
- `melody_similarity(a, b) -> float` — cosine similarity of time-averaged chroma, plus
  frame-level DTW-free alignment: mean cosine over frames after length-normalizing. Range 0..1.
- `timbre_similarity(a, b) -> float` — cosine similarity of CLAP embeddings. Range 0..1
  (rescale from [-1,1]).

Unit tests: identical clip scores ~1.0 on all three; clip vs. white noise scores low;
pitch-shifted clip keeps groove high while melody drops; re-synthesized timbre (e.g., a
lowpassed copy) keeps groove+melody high while timbre drops. Use the committed fixtures.

### 3.3 Embeddings (`embed.py`)

- Single class `Embedder` with `embed_file(path) -> np.ndarray (512,)` and
  `embed_batch(paths) -> np.ndarray`, L2-normalized, with an on-disk cache keyed by
  (file hash, model id) under `~/.cache/diverge/`.
- Used by: reference conditioning, timbre lock, novelty scoring, critic, selection, map.

### 3.4 Generation (`generator.py`)

Define `GeneratorProtocol`:

```python
class GeneratorProtocol(Protocol):
    def generate(self, source: np.ndarray, sr: int, style_embedding: np.ndarray,
                 style_text_hint: str, transform: int, duration_s: float,
                 seed: int, n: int) -> list[np.ndarray]: ...
```

`StableAudioGenerator` (real):
- Model: **Stable Audio Open Small** via `stable_audio_tools` (`get_pretrained_model`).
  Weights downloaded by `scripts/download_models.py` from Hugging Face
  (`stabilityai/stable-audio-open-small`); user must accept the Stability Community
  License — print the URL and require an HF token env var for the download script only.
- SDEdit mechanism: use `generate_diffusion_cond(..., init_audio=(sr, source_tensor),
  init_noise_level=f(transform))`. Map `transform` 0..100 to `init_noise_level`
  approximately over [0.1, 1.0] — CALIBRATE by ear and record the mapping in `NOTES.md`;
  the requirement is monotonic and perceptually roughly linear: 10 ≈ "same loop, new skin",
  90 ≈ "distant cousin".
- Style conditioning, v1 pragmatic path: Stable Audio conditions on text (T5) + timing, not
  on CLAP audio embeddings directly. So: (a) accept a short `style_text_hint` derived from
  the reference filenames/user input as the model-facing prompt; (b) enforce the ACTUAL
  reference steering at the selection layer (3.6) by scoring candidates on CLAP similarity
  to the weighted reference embedding. This is deliberate: reference fidelity is guaranteed
  by selection, not by trusting the prompt. Mark "audio-embedding conditioning / adapters"
  as v2 in NOTES.md.
- Batch: generate `n_oversample` candidates by looping seeds (seed, seed+1, ...). Support a
  `--fast` flag that lowers steps (e.g., 8-step sampler config if available) for iteration.

`MockGenerator` (tests): returns deterministic filtered-noise variants of the source (e.g.,
different bandpass centers + gain envelopes per seed). Must satisfy the protocol so the
entire pipeline, selection, and UI are testable without weights.

### 3.5 Scoring: locks, novelty, taste, reference fit

For each candidate `c` with embedding `e_c`:

- `ref_fit(c)` = cosine(e_c, Σ w_i · e_ref_i) rescaled to 0..1.
- `lock_score(c)` = min over ACTIVE locks of the corresponding similarity to SOURCE
  (groove/melody/timbre from 3.2). Locks are constraints, not preferences.
- `novelty(c)` (`novelty.py`) = normalized distance from the user's library index:
  mean cosine distance to the k=10 nearest neighbors in the index (built by
  `build_library_index.py`, which walks a folder tree, embeds every audio file, and saves
  embeddings + a fitted NearestNeighbors). Also compute `self_novelty(c)`: distance to the
  centroid of the user's last 50 KEPT candidates (from choices.jsonl), if any.
- `taste(c)` (`critic.py`) = probability from a logistic regression over CLAP embeddings,
  trained on choices.jsonl rows `{"path", "embedding_hash", "label": keep|discard, "ts"}`.
  If < 30 labeled rows exist, taste(c) = 0.5 for all (uninformative).

### 3.6 Selection (`select.py`) — where spread and drift actually live

Input: `n_oversample` candidates with all scores. Output: `n_return` winners.

1. HARD FILTER: drop candidates with `lock_score < lock_threshold` (default 0.55 per active
   lock; expose in config). If fewer than `n_return` survive, relax threshold in 0.05 steps
   and record the relaxation in the manifest.
2. UTILITY: `u(c) = α·ref_fit + β·taste + γ·(drift/100)·novelty` with defaults
   α=0.5, β=0.3, γ=0.2·(1 + drift/100). Round and record all weights in the manifest.
3. GREEDY CONSTRAINED FARTHEST-POINT SELECTION: pick the highest-utility candidate first;
   then iteratively pick `argmax_c [ u(c) + λ(spread) · min_{s∈selected} dist(e_c, e_s) ]`
   where `λ` maps spread 0..100 to roughly [0, 1.5] (calibrate; record mapping). This is
   the quality-diversity step: spread=0 degenerates to top-k by utility; spread=100
   approaches pure farthest-point coverage.

Unit tests with synthetic embeddings: spread=0 returns the top-utility cluster;
spread=100 returns points covering the space; locks filter correctly; drift monotonically
increases mean novelty of the selected set.

### 3.7 Session output (`session.py`)

`diverge run ...` writes a bundle `runs/<timestamp>/`:
- `cand_01.wav ... cand_08.wav`
- `manifest.json` — full config, per-candidate scores (ref_fit, each lock similarity,
  novelty, self_novelty, taste, utility), seeds, model ids, calibration mappings used
- `map.json` — 2D UMAP coords of {source, references, candidates} (`map2d.py`; fixed
  random_state; fall back to PCA if n points < UMAP minimum)

### 3.8 Review UI (`review/app.py`)

Gradio app: `python -m review.app runs/<timestamp>`
- Scatter plot from map.json (source = black dot, refs = squares, candidates = numbered)
- Audio player per candidate + its scores
- Keep / Discard buttons per candidate → append rows to `choices.jsonl` (embedding cached)
- "Retrain critic" button → refits logistic regression, saves `models/critic.joblib`,
  prints train accuracy and n
- Keyboard: j/k navigate, y keep, n discard

### 3.9 Phase 1 acceptance criteria

- `pytest -m "not slow"` green with no weights present.
- `diverge run --source data/loop_a.wav --ref data/ref_a.wav:1.0 --transform 40
  --spread 60 --drift 30 --locks groove` completes on an M-series Mac in < 5 min at
  default steps (< 90 s with `--fast`), producing 8 audible, non-silent, non-clipping WAVs.
- Perceptual smoke checklist printed for the human: (1) at transform=15 candidates are
  recognizably the source; (2) at transform=85 they are not; (3) with groove locked,
  tapping along to source and candidate matches; (4) spread=90 batch sounds more varied
  than spread=10 batch. The agent cannot verify these — STOP and ask the human to confirm
  before proceeding to Phase 2.

## 4. Phase 2 — JUCE plugin shell (macOS, AU + VST3)

Architecture: the plugin is a UI + job runner. It does NOT do ML in-process and does NOT
process real-time audio through the model. It shells out to the Phase 1 CLI.

- Scaffold with JUCE 8 via CMake (`FetchContent`). Formats: AU, VST3. Standalone target
  too (fastest iteration loop). macOS only. No AAX. Code signing: ad-hoc for v1; note the
  notarization steps in NOTES.md but do not block on them.
- UI mirrors the agreed mockup: source slot (drag audio in, or capture from the plugin's
  input bus with a record button), two reference slots with weight sliders, lock toggle
  pills, Transform/Spread/Drift sliders, a 2D map canvas plotting map.json, candidate
  detail row with keep/discard, and drag-out of the selected candidate's WAV to the DAW
  (JUCE `performExternalDragDropOfFiles`).
- Job execution: on "Generate", write a RunConfig JSON to a temp path, spawn
  `diverge run --config <path>` as a child process (Python from a bundled venv path set in
  plugin settings), poll for `manifest.json`, then load results. All on a background
  thread; UI stays responsive; show per-candidate progress if the CLI emits progress lines
  on stdout (make the CLI emit `PROGRESS i/N` lines).
- Keep/discard buttons call `diverge critic add <wav> keep|discard` (add this CLI verb in
  Phase 1 if missing).
- Settings panel: python path, models dir, library index path, default output dir.

Acceptance: loads clean in Logic Pro and Ableton Live 12 (AU and VST3 respectively),
`auval` passes for the AU, a full generate→audition→drag-into-track loop works in both,
and killing the plugin mid-generation doesn't orphan zombie processes (child process group
cleanup).

## 5. Phase 3 — critic in the loop

- On plugin launch and after every 10 new choices, retrain the critic automatically
  (spawn `diverge critic train`); surface "taste model: N choices" in the plugin header.
- Selection (3.6) already consumes taste scores; verify end-to-end that keeps visibly
  shift subsequent batches (add an integration test with MockGenerator + synthetic labels:
  after training on "prefers low-band" labels, selected sets skew low-band).
- `self_novelty` joins the utility term with a small weight so batches drift away from the
  user's own recent keeps (anti-self-repetition). Weight in config; default 0.05.

## 6. Explicit non-goals for v1 (record in README so nobody scope-creeps)

- Real-time / audio-thread inference; Windows; AAX; guidance-level negative steering or
  audio-embedding adapters inside the diffusion sampler (v2); training/LoRA on the user's
  catalog (v2); stem separation of candidates (v2 — generate one-shots/loops, not stem
  stacks); cloud rendering tier; any telemetry whatsoever.

## 7. Milestone order for the agent

1. Repo scaffold, config, audio_io, features + tests (no ML deps yet)
2. Embedder + cache + tests (small download; CLAP only)
3. MockGenerator + select.py + novelty + critic + session + CLI, all tested end-to-end mock
4. download_models.py + StableAudioGenerator + `--fast`; first real run; STOP for human
   listening calibration of transform/spread mappings
5. Review UI; accumulate first real choices.jsonl
6. Phase 1 acceptance → Phase 2 JUCE shell → Phase 3 critic loop

At every STOP, print exactly what the human should listen for.
