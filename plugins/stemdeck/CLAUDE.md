# Sattari StemDeck — working notes for a focused session

You're working on **StemDeck in isolation**. It's a DJ-style multi-deck remix
plugin/app (AU + VST3 + Standalone), plugin code `Sd01`, under
`plugins/stemdeck/src`, **app-first** direction. The rest of the Sattari suite is
not your concern here unless you touch `core/`.

For product direction — what StemDeck *is*, the capture-intent architecture, the
build order — read the repo-root `CLAUDE.md`. This file is about building and
working in this plugin.

## Where you are

An ordinary clone of `github.com/armonon/sattari-plugins-suite`, branch `unified`.

`third_party/JUCE` and `third_party/rubberband` are **real checkouts** in this
tree, not symlinks. JUCE must be **8.0.12** (see `/BUILD.md`). `ninja` and
`pluginval` are required and not preinstalled.

## Build & validate (per-plugin — fast, no need to build the whole suite)

```bash
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release -DSATTARI_USE_ONNX=ON
```

```bash
cmake --build build/stemdeck -j
```

**`-DSATTARI_USE_ONNX=ON` is not optional if you want AI Split.** The flag
defaults to **OFF** (`cmake/SattariOnnx.cmake`), so a plain configure silently
prints *"neural runtime disabled — using Demucs CLI + RTSS/DSP fallbacks"* and
compiles the whole on-device neural stack out, even though
`models/neural-model.onnx` and the vendored runtime are both sitting in the tree.
Confirm you got it with:

```
-- StemDeck: on-device neural separation ENABLED (.../third_party/onnxruntime-osx-universal2-*/lib/libonnxruntime.dylib)
```

It must resolve to the **vendored** universal2 runtime in `third_party/`, not
`/opt/homebrew`. Homebrew's build is arm64-only with a macOS 26 floor, so it can
neither go universal nor ship. If `third_party/onnxruntime-osx-universal2-*` is
missing, run `scripts/fetch_onnxruntime.sh` (it is gitignored — a 68 MB dylib
does not belong in git). Trade-off: an ONNX-enabled build raises the deployment
floor to **macOS 13.3**; without it the floor stays 10.13.

Add `-DSATTARI_UNIVERSAL=OFF` for fast host-arch-only iteration.

Machine gate (AU `aumu/Sd01/Satr`, VST3 pluginval@5, and codesign):

```bash
scripts/validate_plugin.sh build/stemdeck/SattariStemDeck_artefacts/Release aumu Sd01 Satr
```

**Standalone codesign quirk:** before validating, re-sign the `.app` with
`codesign --force --deep -s - "<...>/Standalone/Sattari StemDeck.app"` — JUCE's
build-time ad-hoc seal fails `codesign --deep --strict` until re-signed.

## The test targets that matter

All are `EXCLUDE_FROM_ALL`; build by name. These four are the load-bearing gates:

| Target | What it proves |
|---|---|
| `StemDeckReplayHash` | **THE GATE.** Replaying the captured event stream is sample-identical to the live pass. Also probes shutdown with neural refinement in flight. |
| `StemDeckLoadLatency` | Time-to-playable stays under the 200 ms bound (see below). |
| `StemDeckArchitectureContract` | Architecture / beat-sync / key-lock / render contract. |
| `StemDeckLayout` | The layout constraint solver: header items never overlap, the stem waveform keeps its floor. Pure geometry, runs in ms. |

`StemDeckEditorRender` is the visual gate — it renders the whole perform window
offline to a PNG, which is the only practical way to see layout regressions:

```bash
./build/stemdeck/StemDeckEditorRender out.png 1680 1050 4 0 0 1
```

Args are `[out] [W] [H] [channels] [addInput+MIDI] [tab 0-4] [loadWav]`. **Render
at 1280x800 as well as 1680x1050** — the narrow case is where layout breaks, and
it is where the two bugs fixed in the layout overhaul were found.

## Current state (truth-labeled — keep it honest)

Builds, `StemDeckReplayHash` / `StemDeckLoadLatency` / `StemDeckArchitectureContract`
/ `StemDeckLayout` pass. **NOT sale-ready:** no real-song/DAW listening, not
notarized. The packaging plumbing is proven; the hard DSP is not.

Measured, not asserted (`StemDeckLoadLatency`, synthetic material):

```
 30 s track   playable  8 ms   refined  70 ms
120 s track   playable 14 ms   refined 248 ms
300 s track   playable 32 ms   refined 617 ms
```

That is the load-latency problem solved: decks return playable on the DSP result
and neural refinement hot-swaps in later via `refineAnalysisAsync`. **Caveat that
matters:** every number above is from synthetic test signals. Nothing here has
been measured on real, full-length material.

## UI: how layout works now

`resized()` used to be ~750 lines of sequential `removeFromLeft/Bottom(<literal>)`.
That silently produced two bugs, both reproducible with `StemDeckEditorRender` at
1280x800: header items **overlapped** (fixed widths summing to 1528px in a 1232px
bar — `removeFromRight` clamps rather than reporting failure), and the stem
waveform was starved to ~67px by ~311px of fixed-height chrome below it.

`src/LayoutStack.h` replaces that with a constraint solver. Each row/column
declares `min`, `preferred`, `grow` and `priority`; the solver hands out leftover
space by `grow`, shrinks **lowest priority first** when space is tight, and sheds
whole items rather than ever overlapping them. A shed item gets an empty
rectangle, which the existing `if (r.isEmpty()) return;` convention already
handles.

Rules when touching layout:

- **Do not add bare pixel literals.** Use `stemdeck::tokens::gap*` and declare
  intent through `priority`.
- **The waveform has a floor** (`kWaveFloor`) and the highest priority. Chrome
  yields to it, never the reverse — it is the Tier-1 differentiator.
- **Hit targets belong to the layout pass.** `layoutDeckHeaderChips()` computes
  the badge / BPM / key rectangles. They used to be assigned inside `paint()`,
  so clicks between a resize and the next paint used stale geometry. Do not move
  geometry back into `paint()`.
- A row that asks for a large `preferred` squeezes its siblings even on a roomy
  window. If a row should merely *expand into* spare space, set
  `preferred == min` and give it `grow`.

**Colours:** `stemdeck::tokens` (`src/DesignTokens.h`, mirroring
`docs/reference/tokens.css`) is the single source of truth. `stemdeck::palette`
is a deprecated alias namespace kept only for the `using namespace` blocks inside
`StemDeckLookAndFeel.h`; do not name it in new code.

## The real work (big open items) — with file pointers

1. **Sale-grade time/pitch engine** — `src/TimePitchEngine.cpp`, `src/WarpEngine.cpp`,
   `src/BeatSyncEngine.cpp`. Currently a granular key-lock backend; not DAW-validated.
   Rubber Band is available via `sattari_enable_rubberband()` (already linked —
   GPLv3, see `/LICENSING.md`).
2. **Beat peak-picking** — `NeuralBeatEngine.cpp` still peak-picks the model's
   activations with a fixed 0.4 threshold. Porting beat_this's DBN is the real
   fix; expect it to fail on ambient intros and breakdowns until then. The 22.05 kHz
   decimation now has a 6th-order Butterworth anti-alias pre-filter (it previously
   had none, folding cymbal energy down into the band the model reads) — that fix
   is **unverified on real material**.
3. **Separation cancellation** — `NeuralStemEngine::separate()` is one blocking
   call with no cancellation hook, so a full-track in-process separation still
   cannot be interrupted at shutdown. The demucs CLI path *is* cancellable
   (`runSeparatorCommand` takes a `shouldCancel` probe and kills the child).
   `separate()` already takes an `onProgress` callback — that is the natural place
   to thread a cancel probe through.
4. **Plugin-host audio routing** — DONE via `src/HostGraph.{h,cpp}`: per-chain
   `AudioProcessorGraph` (deck/mic/master), async off-thread load, series FX rack,
   RT-safe `processChain` (try-lock, no alloc on the audio thread). Scanning is
   **Sattari-only** by design (third-party VST3s crash the host mid-scan).
   **Open caveat:** some Sattari guest plugins (Echo, Space) crash in their OWN
   editor's `resized()` when hosted — a bug in the guest, not here.
5. **Beatgrid / downbeat detection** — implemented in `src/TrackAnalysis.cpp`
   (multiband spectral-flux onset envelope, autocorrelation tempo, Ellis-2007 DP
   beat tracker, variable-tempo curve, bar-phase downbeat/meter). Not DAW-validated
   on real songs, and there is still no user-editable beatgrid UI.
6. **Notarized installer / onboarding** — packaging via
   `scripts/package_stemdeck_app_alpha.sh`.

## Shared core you can reuse (don't reinvent these)

- `core/include/sattari_audio_core/SattariPitchDetector.h` — shared monophonic pitch
  detector + MIDI↔Hz helpers (`midiToHz`, `hzToMidi`, `nearestMidiNote`,
  `snapHzToSemitone`, `foldToOctaveNear`) + scale-snap (`ScaleId`, `snapMidiToScale`).
  `TrackAnalysis.cpp` still has its own `midiNoteToFrequency`; replace with `sattari::dsp::midiToHz`.
- `SattariEnvelopeFollower.h` — `process` (rising peak) + `processFalling` (GR smoothing).
- `SattariHostTransport.h` — `sattari::host::read(*this)` returns `{bpm, ppqPosition,
  isPlaying, hasHostBpm, hasPpq}`. **Drop-in:** replace `if (auto* ph = getPlayHead()) ...`
  blocks with `const auto t = sattari::host::read (*this);`. `MainPluginProcessor.cpp`
  and `TransportSync.cpp` are the two remaining sites.
- `SattariRealtime.h` — `AtomicSnapshot` (lock-free audio→UI publish) + `ScratchBuffer`.
- `SattariKeyNet.h` — `detectTTA()` runs 12 rotated forward passes over a 90-float
  feature and averages the softmaxes (+2.7 exact). Cheap: microseconds, not a
  reason for slow loads.
- `SattariLookAndFeel`, `SattariPresetManager`, `SattariBranding`, `SattariAudioCore.h`
  (harmonic engine: `KeySignature`, `HarmonicWheelCode`).

## Hard rules

- Go through `sattari_add_plugin()` (StemDeck already does). Don't hand-roll `juce_add_plugin`.
- **No allocation/locks/IO on the audio thread**; publish meter/analysis to the UI via `AtomicSnapshot`.
- **Teardown must not block the host.** A `ThreadPool` job that never calls
  `shouldExit()` is not interruptible, and `removeAllJobs(true, -1)` will wait for
  it forever. Any long-running job must poll, and any child process must be
  killable. This is not theoretical — it shipped as a potential 45-minute host
  freeze on quit.
- Suite is **GPLv3** (`/LICENSING.md`).
- Nothing is "sale-ready" until rebuilt + pluginval'd **and** DAW-listened.
- **Do not claim "verified" from synthetic test signals.** Say which material a
  number came from.
