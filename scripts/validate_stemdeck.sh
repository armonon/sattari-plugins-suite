#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build/stemdeck}"
strictness="${PLUGINVAL_STRICTNESS:-5}"
pluginval_bin="${PLUGINVAL_BIN:-/Applications/pluginval.app/Contents/MacOS/pluginval}"

cd "$repo_root"

./scripts/validate_sources.sh

cmake -S plugins/stemdeck -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --config Release --target SattariStemDeck_Standalone SattariStemDeck_VST3 -j "${JOBS:-4}"
cmake --build "$build_dir" --config Release --target StemDeckGranularRenderContract -j "${JOBS:-4}"
echo "--- StemDeck granular render contract"
"$build_dir/StemDeckGranularRenderContract"
cmake --build "$build_dir" --config Release --target StemDeckTimePitchBenchmark -j "${JOBS:-4}"
echo "--- StemDeck time/pitch benchmark"
"$build_dir/StemDeckTimePitchBenchmark" "$build_dir/timepitch-benchmark"
cmake --build "$build_dir" --config Release --target StemDeckArchitectureContract -j "${JOBS:-4}"
echo "--- StemDeck architecture contract"
"$build_dir/StemDeckArchitectureContract"

vst3="$build_dir/SattariStemDeck_artefacts/Release/VST3/Sattari StemDeck.vst3"
app="$build_dir/SattariStemDeck_artefacts/Release/Standalone/Sattari StemDeck.app"

for artifact in "$vst3" "$app"; do
  if [[ ! -e "$artifact" ]]; then
    echo "missing expected artifact: $artifact" >&2
    exit 1
  fi

  # Raw JUCE/CMake bundles can retain stale ad-hoc signatures after relink.
  # Refresh local build signatures before strict verification.
  codesign --force --deep --sign - "$artifact" >/dev/null
  echo "--- codesign verify: $artifact"
  codesign --verify --deep --strict --verbose=2 "$artifact"
done

if [[ -x "$pluginval_bin" ]]; then
  echo "--- pluginval strictness $strictness: $vst3"
  "$pluginval_bin" --validate-in-process --strictness-level "$strictness" "$vst3"
else
  echo "pluginval not found at $pluginval_bin; set PLUGINVAL_BIN or install pluginval to run VST3 validation." >&2
fi
