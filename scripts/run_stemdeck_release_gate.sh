#!/usr/bin/env bash
# Reproducible local/CI release gate: fresh configure, every deterministic
# contract, all macOS formats, self-contained staging, host validation, archive.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${STEMDECK_GATE_BUILD_DIR:-$repo_root/build/stemdeck-release-gate}"
jobs="${JOBS:-3}"
fresh="${STEMDECK_GATE_FRESH:-1}"

for tool in cmake ninja codesign; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing required tool: $tool" >&2; exit 2; }
done

if [[ "$fresh" == 1 && -d "$build_dir" ]]; then
  rm -rf "$build_dir"
fi

cmake_args=(
  -S "$repo_root/plugins/stemdeck"
  -B "$build_dir"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON
  -DSATTARI_USE_ONNX=ON
  -DSTEMDECK_PRODUCT_LTO=OFF
)
if command -v ccache >/dev/null 2>&1; then
  cmake_args+=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
  ccache --zero-stats >/dev/null || true
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --target StemDeckContractSuite \
  SattariStemDeck_Standalone SattariStemDeck_AU SattariStemDeck_VST3 -j "$jobs"
ctest --test-dir "$build_dir" --output-on-failure -L contract -j "$jobs"

release="$build_dir/SattariStemDeck_artefacts/Release"
stage="$build_dir/validated-stage"
rm -rf "$stage"
mkdir -p "$stage"
ditto "$release/Standalone/Sattari StemDeck.app" "$stage/Sattari StemDeck.app"
ditto "$release/AU/Sattari StemDeck.component" "$stage/Sattari StemDeck.component"
ditto "$release/VST3/Sattari StemDeck.vst3" "$stage/Sattari StemDeck.vst3"

for bundle in "$stage/Sattari StemDeck.app" \
              "$stage/Sattari StemDeck.component" \
              "$stage/Sattari StemDeck.vst3"; do
  "$repo_root/scripts/selfcontain_onnx.sh" "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"
done
"$repo_root/scripts/validate_stemdeck_app_bundle.sh" "$stage/Sattari StemDeck.app"

if [[ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
  /Applications/pluginval.app/Contents/MacOS/pluginval \
    --validate-in-process --strictness-level 5 "$stage/Sattari StemDeck.vst3"
fi

if [[ "$(uname -s)" == Darwin ]]; then
  au_log="$build_dir/auval.log"
  /usr/bin/auval -v aumu Sd01 Satr >"$au_log" 2>&1
  grep -E 'Time to open AudioUnit|Global Scope Parameters|AU VALIDATION SUCCEEDED' "$au_log"
  parameter_count="$(sed -n 's/.*# # # \([0-9][0-9]*\) Global Scope Parameters:.*/\1/p' "$au_log" | head -1)"
  [[ -n "$parameter_count" && "$parameter_count" -le 330 ]] || {
    echo "AU parameter budget exceeded: ${parameter_count:-unknown} (limit 330)" >&2
    exit 1
  }
fi

archive="$build_dir/Sattari-StemDeck-validated.zip"
rm -f "$archive"
ditto -c -k --sequesterRsrc --keepParent "$stage" "$archive"
shasum -a 256 "$archive" > "$archive.sha256"

if command -v ccache >/dev/null 2>&1; then
  ccache --show-stats || true
fi
echo "StemDeck release gate passed: $archive"
