#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$repo_root/../.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build/stemdeck-app}"
out_root="${1:-$workspace_root/deliverables/sattari-test-builds}"
version="${STEMDECK_APP_VERSION:-0.5.0-studio-workflow}"
use_onnx="${SATTARI_USE_ONNX:-OFF}"
package_dir="$out_root/stemdeck-app-alpha-v$version"
release_dir="$build_dir/SattariStemDeck_artefacts/Release"
app_src="$release_dir/Standalone/Sattari StemDeck.app"

cd "$repo_root"

mkdir -p "$out_root"
./scripts/validate_sources.sh

cmake_args=(
  -S plugins/stemdeck
  -B "$build_dir"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  "-DSATTARI_USE_ONNX=$use_onnx"
)
if [[ -n "${ONNXRUNTIME_ROOT:-}" ]]; then
  cmake_args+=("-DONNXRUNTIME_ROOT=$ONNXRUNTIME_ROOT")
fi
cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config Release --target SattariStemDeck_Standalone -j "${JOBS:-4}"
cmake --build "$build_dir" --config Release --target StemDeckGranularRenderContract -j "${JOBS:-4}"
echo "--- StemDeck granular render contract"
"$build_dir/StemDeckGranularRenderContract"
cmake --build "$build_dir" --config Release --target StemDeckTimePitchBenchmark -j "${JOBS:-4}"
echo "--- StemDeck time/pitch benchmark"
"$build_dir/StemDeckTimePitchBenchmark" "$build_dir/timepitch-benchmark"
cmake --build "$build_dir" --config Release --target StemDeckArchitectureContract -j "${JOBS:-4}"
echo "--- StemDeck architecture contract"
"$build_dir/StemDeckArchitectureContract"

if [[ ! -d "$app_src" ]]; then
  echo "missing expected app: $app_src" >&2
  exit 1
fi

rm -rf "$package_dir"
mkdir -p "$package_dir/Sattari StemDeck" "$package_dir/scripts" "$package_dir/docs" "$package_dir/validation"

ditto "$app_src" "$package_dir/Sattari StemDeck/Sattari StemDeck.app"
app_bundle="$package_dir/Sattari StemDeck/Sattari StemDeck.app"
app_binary="$app_bundle/Contents/MacOS/Sattari StemDeck"
frameworks_dir="$app_bundle/Contents/Frameworks"
mkdir -p "$frameworks_dir"

bundle_optional_dylib() {
  local source_path="$1"
  local dylib_name="$2"
  if [[ -e "$source_path" ]]; then
    ditto "$source_path" "$frameworks_dir/$dylib_name"
    /usr/bin/install_name_tool -id "@rpath/$dylib_name" "$frameworks_dir/$dylib_name" || true
  fi
}

bundle_optional_dylib "/opt/homebrew/opt/libsamplerate/lib/libsamplerate.0.dylib" "libsamplerate.0.dylib"
bundle_optional_dylib "/opt/homebrew/opt/rubberband/lib/librubberband.3.dylib" "librubberband.3.dylib"

if [[ -e "$frameworks_dir/librubberband.3.dylib" && -e "$frameworks_dir/libsamplerate.0.dylib" ]]; then
  /usr/bin/install_name_tool -change "/opt/homebrew/opt/libsamplerate/lib/libsamplerate.0.dylib" "@rpath/libsamplerate.0.dylib" "$frameworks_dir/librubberband.3.dylib" || true
  /usr/bin/install_name_tool -change "/opt/homebrew/opt/rubberband/lib/librubberband.3.dylib" "@rpath/librubberband.3.dylib" "$app_binary" || true
  /usr/bin/install_name_tool -change "/opt/homebrew/opt/libsamplerate/lib/libsamplerate.0.dylib" "@rpath/libsamplerate.0.dylib" "$app_binary" || true
  /usr/bin/install_name_tool -add_rpath "@executable_path/../Frameworks" "$app_binary" 2>/dev/null || true
fi

# A neural build is allowed only with an explicitly selected, deployment-floor
# compatible runtime (CMake enforces that). Make that runtime self-contained just
# like Rubber Band: copy the exact dylib the binary linked, rewrite the load
# command to @rpath, then let the bundle validator prove there are no machine-local
# paths left. Ordinary packages have SATTARI_USE_ONNX=OFF and skip this entirely.
onnx_link="$(otool -L "$app_binary" | awk '/libonnxruntime.*\\.dylib/{print $1; exit}')"
if [[ -n "$onnx_link" ]]; then
  [[ -f "$onnx_link" ]] || {
    echo "linked ONNX Runtime does not exist: $onnx_link" >&2
    exit 1
  }
  onnx_real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$onnx_link")"
  onnx_name="$(basename "$onnx_link")"
  ditto "$onnx_real" "$frameworks_dir/$onnx_name"
  /usr/bin/install_name_tool -id "@rpath/$onnx_name" "$frameworks_dir/$onnx_name"
  /usr/bin/install_name_tool -change "$onnx_link" "@rpath/$onnx_name" "$app_binary"
  /usr/bin/install_name_tool -add_rpath "@executable_path/../Frameworks" "$app_binary" 2>/dev/null || true
  echo "--- bundled ONNX Runtime: $onnx_name"
fi

# Optionally bundle the on-device neural models INTO the app so premium AI stem
# separation and neural beat tracking work with zero manual install. Opt-in because
# the models are large (~353MB stem + ~84MB beat): set STEMDECK_BUNDLE_MODELS=1.
# NeuralStemEngine/NeuralBeatEngine::defaultModelFile() look in Contents/Resources.
if [[ "${STEMDECK_BUNDLE_MODELS:-0}" == "1" ]]; then
  resources_dir="$app_bundle/Contents/Resources"
  mkdir -p "$resources_dir"
  bundle_optional_model() {
    local src="$repo_root/models/$1"
    if [[ -f "$src" ]]; then
      echo "--- bundling model $1 ($(du -h "$src" | cut -f1))"
      ditto "$src" "$resources_dir/$1"
    else
      echo "--- model $1 not found at $src (skipped)"
    fi
  }
  bundle_optional_model "neural-model.onnx"
  bundle_optional_model "beat-model.onnx"
else
  echo "--- STEMDECK_BUNDLE_MODELS!=1: shipping without embedded neural models (users install to ~/Library or set the model env var)"
fi

/usr/bin/xattr -cr "$app_bundle" || true
/bin/chmod -R u+rwX,go+rX "$app_bundle"
codesign --force --deep --sign - "$app_bundle" >/dev/null
codesign --verify --deep --strict --verbose=2 "$app_bundle"

if otool -L "$app_binary" | grep -q '/opt/homebrew'; then
  echo "packaged app still references /opt/homebrew dylibs" >&2
  otool -L "$app_binary" >&2
  exit 1
fi
if [[ -e "$frameworks_dir/librubberband.3.dylib" ]] && otool -L "$frameworks_dir/librubberband.3.dylib" | grep -q '/opt/homebrew'; then
  echo "bundled librubberband still references /opt/homebrew dylibs" >&2
  otool -L "$frameworks_dir/librubberband.3.dylib" >&2
  exit 1
fi

if [[ -d "$build_dir/timepitch-benchmark" ]]; then
  ditto "$build_dir/timepitch-benchmark" "$package_dir/validation/timepitch-benchmark"
fi

ditto "$repo_root/plugins/stemdeck/docs/stemdeck-sale-readiness-gates.md" "$package_dir/docs/stemdeck-sale-readiness-gates.md"
ditto "$repo_root/scripts/setup_stemdeck_demucs.sh" "$package_dir/scripts/setup_stemdeck_demucs.sh"
ditto "$repo_root/scripts/stemdeck_separate_demucs.py" "$package_dir/scripts/stemdeck_separate_demucs.py"
ditto "$repo_root/scripts/stemdeck_separate_demucs_venv.sh" "$package_dir/scripts/stemdeck_separate_demucs_venv.sh"
ditto "$repo_root/scripts/validate_stemdeck_app_bundle.sh" "$package_dir/scripts/validate_stemdeck_app_bundle.sh"

cat > "$package_dir/scripts/install_app.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
pkg_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="$pkg_root/Sattari StemDeck/Sattari StemDeck.app"
if [[ ! -d "$app" ]]; then
  echo "Missing Sattari StemDeck.app at $app" >&2
  exit 1
fi
mkdir -p "$HOME/Applications"
ditto "$app" "$HOME/Applications/Sattari StemDeck.app"
/usr/bin/xattr -cr "$HOME/Applications/Sattari StemDeck.app" || true
/bin/chmod -R u+rwX,go+rX "$HOME/Applications/Sattari StemDeck.app"
echo "Installed Sattari StemDeck.app to $HOME/Applications"
INSTALL

cat > "$package_dir/scripts/validate_app.sh" <<'VALIDATE'
#!/usr/bin/env bash
set -euo pipefail
pkg_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$pkg_root/scripts/validate_stemdeck_app_bundle.sh" "$pkg_root"
plutil -p "$pkg_root/Sattari StemDeck/Sattari StemDeck.app/Contents/Info.plist" | sed -n '1,80p'
otool -L "$pkg_root/Sattari StemDeck/Sattari StemDeck.app/Contents/MacOS/Sattari StemDeck" | sed -n '1,80p'
VALIDATE

chmod +x "$package_dir/scripts/"*.sh

cat > "$package_dir/README.md" <<EOF_README
# Sattari StemDeck app alpha v$version

Truth label: **standalone Mac DJ app alpha**. This is an app-first StemDeck package, not a DAW plugin package and not a sale-ready release.

## Open it like an application

The app is here:

\`Sattari StemDeck/Sattari StemDeck.app\`

To install it for the current user:

\`scripts/install_app.sh\`

That copies the app to:

\`~/Applications/Sattari StemDeck.app\`

## What this app is

- A standalone native macOS StemDeck/DJ application built from the same JUCE engine.
- Bundled Rubber Band + libsamplerate dylibs inside the app so it does not require Homebrew to launch.
- Header-level quick-start rail inside the app: load/drag a full mix → optional local-Demucs AI Split → mix with faders/FX/pads → Record WAV.
- Split DJ/DAW workspace: top live mixer/stem decks, middle master/crossfader/record controls, bottom arrangement recorder lanes, right-side performance pads.
- Performance-capture direction: record the performed mix into arrangement-style lanes/metadata and master/per-deck files.
- Includes optional local Demucs setup helpers for offline stem separation experiments.

## What this app is not yet

- Not notarized yet; macOS may still show the usual alpha/Gatekeeper warning.
- Not a sale-ready release.
- Not yet a complete editable Ableton-style DAW timeline with clip dragging, comping, arrangement editing, or polished export workflow.
- Not a Windows/Linux app; this package is for Apple Silicon macOS alpha testing.

## Validation included

- Standalone app CMake/Ninja build.
- Codesign strict verification on the packaged app.
- Homebrew dylib reference check: packaged app and bundled Rubber Band must not reference \`/opt/homebrew\`.
- StemDeck granular render contract.
- StemDeck time/pitch benchmark.
- StemDeck architecture contract.
EOF_README

{
  echo "package: $(basename "$package_dir")"
  echo "product: Sattari StemDeck"
  echo "version: $version"
  echo "truth_label: standalone Mac DJ app alpha; not sale-ready; not notarized"
  echo "created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "source_commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "source_repo: $repo_root"
  echo "build_dir: $build_dir"
  echo "formats: Standalone app"
  if [[ -e "$frameworks_dir/librubberband.3.dylib" ]]; then
    echo "bundled_dylib: Contents/Frameworks/librubberband.3.dylib"
  fi
  if [[ -e "$frameworks_dir/libsamplerate.0.dylib" ]]; then
    echo "bundled_dylib: Contents/Frameworks/libsamplerate.0.dylib"
  fi
  if [[ -n "$onnx_link" ]]; then
    echo "bundled_dylib: Contents/Frameworks/$onnx_name"
  fi
  echo "app: Sattari StemDeck/Sattari StemDeck.app"
} > "$package_dir/MANIFEST.txt"

(cd "$out_root" && tar -czf "$(basename "$package_dir").tar.gz" "$(basename "$package_dir")")

echo "packaged $package_dir"
echo "archive $package_dir.tar.gz"
