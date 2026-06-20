#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build/voxkey}"
release_dir="$build_dir/SattariVoxKey_artefacts/Release"
pluginval_bin="${PLUGINVAL_BIN:-pluginval}"

cd "$repo_root"

test -f plugins/voxkey/src/PluginProcessor.cpp
test -f plugins/voxkey/src/PitchDetector.cpp
test -f plugins/voxkey/src/VoiceMidiMapper.cpp
echo "source layout ok"

cmake -S plugins/voxkey -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --config Release -j "${JOBS:-4}"

echo "--- VoxKey core contract"
"$build_dir/VoxKeyCoreContract"

vst3="$release_dir/VST3/Sattari VoxKey.vst3"
au="$release_dir/AU/Sattari VoxKey.component"
standalone="$release_dir/Standalone/Sattari VoxKey.app"

echo "--- codesign verify: $vst3"
codesign --force --deep --sign - "$vst3" >/dev/null
codesign --verify --deep --strict --verbose=2 "$vst3"

if [[ -d "$au" ]]; then
  echo "--- codesign verify: $au"
  codesign --force --deep --sign - "$au" >/dev/null
  codesign --verify --deep --strict --verbose=2 "$au"
fi

if [[ -d "$standalone" ]]; then
  echo "--- codesign verify: $standalone"
  codesign --force --deep --sign - "$standalone" >/dev/null
  codesign --verify --deep --strict --verbose=2 "$standalone"
fi

if command -v "$pluginval_bin" >/dev/null 2>&1; then
  echo "--- pluginval strictness 5: $vst3"
  "$pluginval_bin" --strictness-level 5 --validate "$vst3"
else
  echo "pluginval not found; skipped"
fi
