#!/usr/bin/env bash
# Reproducible local/CI release gate: fresh configure, every deterministic
# contract, all macOS formats, self-contained staging, host validation, archive.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${STEMDECK_GATE_BUILD_DIR:-$repo_root/build/stemdeck-release-gate}"
jobs="${JOBS:-3}"
fresh="${STEMDECK_GATE_FRESH:-1}"

for tool in cmake ninja codesign ditto shasum; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing required tool: $tool" >&2; exit 2; }
done

if [[ "$fresh" == 1 && -d "$build_dir" ]]; then
  rm -rf "$build_dir"
fi

# The gate asks for -DSATTARI_USE_ONNX=ON below, but cmake/SattariOnnx.cmake handles
# "requested but not found" with message(WARNING) and CARRIES ON with the define set to
# 0 — so on a fresh clone (third_party/onnxruntime-* is gitignored) the entire neural
# stack compiles out and every downstream step still reports success: the smoke tests
# skip cleanly, StemDeckStemCancelSmoke does not exist so the if(TARGET) guard drops it
# silently, selfcontain_onnx.sh prints "skip (no onnx link)", the bundle validator
# reports onnxruntime_bundled: false and validation OK, and the gate prints "passed".
#
# That is this project's single worst historical bug — the neural stack silently
# compiled out — reproduced inside the artifact built to prevent it. release_stemdeck.sh
# already has the correct hard guard; this is the same one.
use_onnx="${SATTARI_USE_ONNX:-ON}"
if [[ "$use_onnx" == "ON" ]]; then
  if [[ -z "$(echo "$repo_root"/third_party/onnxruntime-osx-universal2-*/lib/libonnxruntime.dylib 2>/dev/null | grep -v '\*')" ]]; then
    echo "SATTARI_USE_ONNX=ON but no vendored ONNX Runtime found." >&2
    echo "Run scripts/fetch_onnxruntime.sh first, or set SATTARI_USE_ONNX=OFF." >&2
    echo "Refusing to 'pass' a gate that would silently compile the neural stack out." >&2
    exit 2
  fi
fi

cmake_args=(
  -S "$repo_root/plugins/stemdeck"
  -B "$build_dir"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON
  -DSATTARI_USE_ONNX="$use_onnx"
  # STEMDECK_PRODUCT_LTO defaults ON and release_stemdeck.sh does not override it, so
  # releases ship WITH LTO. This gate used to hardcode OFF while claiming to validate
  # "the exact O3 product" — whole-program LTO changes inlining, codegen and UB
  # exposure, so that claim was false. Default to matching what ships; override to OFF
  # only for fast local iteration, knowing you are then testing a different binary.
  -DSTEMDECK_PRODUCT_LTO="${STEMDECK_GATE_LTO:-ON}"
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
else
  # Say so out loud. A silently-skipped check reads as a passed check in a gate log,
  # and this gate's pass message does not distinguish the two.
  echo "WARNING: pluginval not installed at /Applications/pluginval.app — VST3 host" >&2
  echo "         validation was SKIPPED, not passed." >&2
fi

if [[ "$(uname -s)" == Darwin ]]; then
  # auval resolves components through the system/user plug-in registry, NOT from a path.
  # This gate staged its .component and then ran auval without installing it, so on any
  # machine with a previously-installed StemDeck AU it validated THAT — a stale binary,
  # not the artifact in the archive. Install the staged component first, and restore
  # whatever was there when we are done.
  au_dest="$HOME/Library/Audio/Plug-Ins/Components/Sattari StemDeck.component"
  au_backup=""
  if [[ -e "$au_dest" ]]; then
    au_backup="$(mktemp -d "${TMPDIR:-/tmp}/stemdeck-au-backup.XXXXXX")/Sattari StemDeck.component"
    ditto "$au_dest" "$au_backup"
  fi
  restore_au() {
    rm -rf "$au_dest"
    if [[ -n "$au_backup" && -e "$au_backup" ]]; then
      ditto "$au_backup" "$au_dest"
      rm -rf "$(dirname "$au_backup")"
    fi
  }
  trap restore_au EXIT
  mkdir -p "$(dirname "$au_dest")"
  rm -rf "$au_dest"
  ditto "$stage/Sattari StemDeck.component" "$au_dest"
  killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

  au_log="$build_dir/auval.log"
  /usr/bin/auval -v aumu Sd01 Satr >"$au_log" 2>&1

  # Was grep -E 'Time to open AudioUnit|Global Scope Parameters|AU VALIDATION SUCCEEDED'.
  # "Time to open AudioUnit" is printed during the open phase, long before any verdict,
  # so that alternation matched a log ending in AU VALIDATION FAILED. Only the verdict
  # line means the thing the check was written to mean.
  grep -q 'AU VALIDATION SUCCEEDED' "$au_log" || {
    echo "auval did not report AU VALIDATION SUCCEEDED — see $au_log" >&2
    tail -40 "$au_log" >&2
    exit 1
  }
  grep -E 'Time to open AudioUnit|Global Scope Parameters|AU VALIDATION SUCCEEDED' "$au_log" || true

  parameter_count="$(sed -n 's/.*# # # \([0-9][0-9]*\) Global Scope Parameters:.*/\1/p' "$au_log" | head -1)"
  [[ -n "$parameter_count" && "$parameter_count" -le 330 ]] || {
    echo "AU parameter budget exceeded or unreadable: ${parameter_count:-unknown} (limit 330)" >&2
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
