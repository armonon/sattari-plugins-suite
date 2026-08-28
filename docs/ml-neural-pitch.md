# Neural Pitch (CREPE) — provisioning guide

Auto Pitch can use a **CREPE** convolutional pitch model instead of the built-in
YIN tracker. CREPE (Kim et al., ICASSP 2018) is the accuracy leader for
monophonic pitch and, crucially, does not octave-jump on breathy / noisy vocals —
the failure mode that makes correction feel "off". The model is **optional** and
not bundled; the plugin runs YIN until you install it.

## What's already in the tree

| Piece | Where | Status |
|---|---|---|
| Inference wrapper | `core/include/sattari_audio_core/SattariCrepePitch.h` | done, unit-tested (`SattariAudioCoreCrepeContract`) |
| Salience→Hz decode | same header, pure-std | done (+0.00 cents on synthetic spikes) |
| ONNX build seam | `cmake/SattariOnnx.cmake` (`SATTARI_USE_ONNX` + required release mode) | done |
| Worker thread | `plugins/auto-pitch` — inference runs off the audio thread | done |
| UI toggle | Pro tab → **Neural Pitch** | done |
| The model file | `~/Library/Sattari Audio/models/crepe.onnx` | **you generate it (below)** |

The audio thread only writes a lock-free ring; a background worker runs the ONNX
`Run()` and publishes the result into atomics the audio thread reads. So enabling
Neural Pitch never blocks or allocates on the realtime path.

## Three steps to turn it on

### 1. Fetch the pinned ONNX Runtime build dependency

```bash
./scripts/fetch_onnxruntime_macos.sh
```

The script downloads Microsoft's universal2 1.22.0 release and verifies SHA-256
`cfa6f6584d87555ed9f6e7e8a000d3947554d589efe3723b8bfa358cd263d03c` before
extracting it into ignored `third_party/` state.

### 2. Generate the model

TensorFlow (needed only for this one-time conversion) does not yet support
Python 3.13/3.14, so use a 3.10–3.12 environment:

```bash
python3.11 -m venv .crepe-venv && source .crepe-venv/bin/activate
pip install "crepe==0.0.15" "tf2onnx>=1.16" "tensorflow>=2.12,<2.17" onnx
python tools/export_crepe_onnx.py --capacity tiny
# -> ~/Library/Sattari Audio/models/crepe.onnx   (487 KB for 'tiny')
```

`tiny` and `small` are the realtime-sensible capacities; `medium`/`large`/`full`
are more accurate but heavier.

### 3. Build with ONNX enabled

The release-shaped path is one command:

```bash
./scripts/build_auto_pitch_neural.sh
```

It requires ONNX instead of silently falling back, embeds the runtime in all
three bundles, rejects local absolute rpaths, and runs the core, render, and
objective performance/safety benchmark checks.
The official ONNX Runtime 1.22 universal dylib has a macOS 13.3 deployment
minimum, so the neural Auto Pitch build declares macOS 13.3+.

> **Architecture note (important):** the suite defaults to a **universal**
> (arm64 + x86_64) build, but Homebrew's `onnxruntime` on Apple Silicon is
> **arm64-only** — linking it into a universal build fails on the x86_64 slice
> with `Undefined symbols: _OrtGetApiBase`. Two ways to get ONNX:

**A. Universal build (recommended)** — use Microsoft's official **universal2**
release, which has both slices:

```bash
curl -L -o /tmp/ort.tgz \
  https://github.com/microsoft/onnxruntime/releases/download/v1.22.0/onnxruntime-osx-universal2-1.22.0.tgz
tar -xzf /tmp/ort.tgz -C third_party/
ORT="$PWD/third_party/onnxruntime-osx-universal2-1.22.0"

# find_library caches the first hit, so set the cache vars EXPLICITLY (a plain
# ONNXRUNTIME_ROOT is ignored if a previous configure already cached brew's lib):
cmake -S . -B build/local -G Ninja -DSATTARI_USE_ONNX=ON \
      -DONNXRUNTIME_INCLUDE_DIR="$ORT/include" \
      -DONNXRUNTIME_LIB="$ORT/lib/libonnxruntime.dylib"
cmake --build build/local --target SattariAutoPitch_Standalone
```

**B. arm64-only build** — simplest with brew's arm64 lib, separate build dir:

```bash
cmake -S . -B build/onnx -G Ninja \
      -DSATTARI_USE_ONNX=ON -DSATTARI_UNIVERSAL=OFF -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/onnx --target SattariAutoPitch_Standalone
```

Then in the Pro tab, turn on **Neural Pitch**. If the model is missing or the
build has no ONNX, the toggle is inert and YIN is used — no crash, no silence.

> **Distribution note:** `scripts/build_auto_pitch_neural.sh` embeds the dylib in
> `Contents/Frameworks`, rewrites the rpath, signs the bundle, and runs
> `scripts/verify_auto_pitch_bundle.sh`. Do not distribute an unverified raw
> build-tree bundle.

Create the traceable internal DAW/listening kit after the build passes:

```bash
./scripts/package_auto_pitch_neural_tester.sh
```

The kit includes universal AU, VST3, and standalone bundles, local install and
validation helpers, exact binary/source fingerprints, and no neural weights.
It requires all three local models by default so a tester candidate cannot be
assembled after silently skipping the neural surface. Set
`SATTARI_TESTER_ALLOW_MODEL_FALLBACK=1` only when deliberately testing fallback
behavior.

Verify the C++ inference path end-to-end (works from either build dir):

```bash
cmake --build build/local --target AutoPitchRender
./build/local/plugins/auto-pitch/AutoPitchRender --crepe   # loads crepe.onnx, tracks test tones
```

## Model I/O contract

The wrapper expects the standard CREPE signature:

* **input**  `[1, 1024]` float32, one frame at 16 kHz, per-frame zero-mean/unit-std
* **output** `[1, 360]` salience over 20-cent bins, `cents(i) = 1997.379 + 20·i`,
  `Hz = 10 · 2^(cents/1200)`

`export_crepe_onnx.py` freezes exactly this signature, so any CREPE capacity drops
in without code changes.

## Honest limitations

* The runtime is reproducible, but the three neural model files are external.
  Do not bundle them until export/training provenance and redistribution rights
  are documented.
* `scripts/verify_auto_pitch_models.py` fingerprints local models. Passing its
  normal audit is not redistribution approval. Its `--distribution --record`
  mode requires a matching SHA-256, source URL, license, and explicit clearance;
  start from `config/auto-pitch-model-release-record.example.json`.
* Rubber Band remains the correction engine; CREPE only replaces the *pitch
  estimate* feeding correction. Pair with the PSOLA **Live Engine** for the
  lowest-latency monitor path.
