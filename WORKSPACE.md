# StemDeck-Full — complete independent workspace

This is a **fully self-contained, buildable clone** of the Sattari StemDeck project
(the whole `sattari-plugins-suite`) on branch `te-migration`, made 2026-07-12. Work
here from now on — it has full git history, all source, and every build dependency
bundled in.

## What's included
- **Full git history** — cloned from the local suite repo; `origin` points at
  `github.com/armonon/sattari-plugins-suite` so `git push` goes to GitHub.
- **All dependencies materialized as real files** (not symlinks/submodules):
  - `third_party/JUCE` (109 MB)
  - `third_party/rubberband` (1.7 MB)
  - `third_party/tracktion_engine` (79 MB, v3.2.0)
- **`models/neural-model.onnx`** — the Demucs separation model (353 MB).
- ONNX Runtime is used from Homebrew (`/opt/homebrew`) at build time; the shipped
  `.app` bundles it so the app itself is portable.

## Build
```bash
cd ~/Desktop/StemDeck-Full
# first time — arm64 is REQUIRED (Homebrew onnxruntime is arm64-only; a universal
# build fails to link with "Undefined symbols for architecture x86_64")
cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/stemdeck --target SattariStemDeck_Standalone -j8
```
The app lands at
`build/stemdeck/SattariStemDeck_artefacts/Release/Standalone/Sattari StemDeck.app`.

### Test targets (optional)
```bash
cmake --build build/stemdeck --target StemDeckKeyDetectTest -j8      # key detection
cmake --build build/stemdeck --target StemDeckAutomixTest -j8        # automix planner
cmake --build build/stemdeck --target StemDeckResamplerTest -j8      # sinc resampler
cmake --build build/stemdeck --target StemDeckBeatDetectTest -j8     # beat detection
cmake --build build/stemdeck --target StemDeckNeuralSmoke -j8        # neural (needs a WAV + model)
```

## Neural separation
The app reads the model from `~/Library/Sattari/StemDeck/StemProvider/neural-model.onnx`
(already installed on this machine). A copy is in `models/` here for portability —
to (re)install it: `cp models/neural-model.onnx ~/Library/Sattari/StemDeck/StemProvider/`.

## Important
- The CMake build is configured for THIS path. If you move the folder, delete
  `build/` and re-run the configure step.
- `third_party/JUCE` and `third_party/tracktion_engine` are declared as submodules
  in `.gitmodules` but are populated here as plain files so the build is offline and
  self-contained. Don't run `git submodule update` unless you intend to switch them
  back to real submodules.
