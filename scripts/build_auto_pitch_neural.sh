#!/usr/bin/env bash
# Build and prove the distribution-shaped Auto Pitch neural binary.
# ONNX Runtime 1.22's official universal2 dylib requires macOS 13.3, so this
# dedicated lane uses that honest floor without raising it for the whole suite.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SATTARI_AUTOPITCH_BUILD_DIR:-$repo_root/build/auto-pitch-neural}"
ort_dir="$($repo_root/scripts/fetch_onnxruntime_macos.sh)"
jobs="${SATTARI_BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
validation_dir="$build_dir/validation"
mkdir -p "$validation_dir"

cmake -S "$repo_root/plugins/auto-pitch" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
  -DSATTARI_USE_ONNX=ON \
  -DSATTARI_REQUIRE_ONNX=ON \
  -DONNXRUNTIME_INCLUDE_DIR="$ort_dir/include" \
  -DONNXRUNTIME_LIB="$ort_dir/lib/libonnxruntime.dylib"

grep -q -- '-DSATTARI_HAS_ONNX=1' "$build_dir/build.ninja" || {
  echo "configured build did not compile the ONNX path" >&2
  exit 1
}

cmake --build "$build_dir" --config Release -j"$jobs"
cmake --build "$build_dir" --target AutoPitchCoreContract AutoPitchRender AutoPitchBench -j"$jobs"

contract_bin="$(find "$build_dir" -type f -name AutoPitchCoreContract -perm +111 | head -n1)"
render_bin="$(find "$build_dir" -type f -name AutoPitchRender -perm +111 | head -n1)"
bench_bin="$(find "$build_dir" -type f -name AutoPitchBench -perm +111 | head -n1)"
[[ -n "$contract_bin" && -n "$render_bin" && -n "$bench_bin" ]] || {
  echo "Auto Pitch validation binaries were not produced" >&2
  exit 1
}
"$contract_bin"
"$render_bin" --smoke
"$render_bin" --hitracktest
"$render_bin" --polystress
"$render_bin" --features
"$render_bin" --gainstage
"$render_bin" --vibtest
"$render_bin" --polytest
"$render_bin" --xposetest
"$bench_bin" | tee "$validation_dir/objective-benchmark.txt"

model_dir="$HOME/Library/Sattari Audio/models"
"$repo_root/scripts/verify_auto_pitch_models.py" --model-dir "$model_dir"
if [[ -f "$model_dir/crepe.onnx" ]]; then
  "$render_bin" --crepe
else
  echo "CREPE model proof skipped: $model_dir/crepe.onnx is not installed"
fi
if [[ -f "$model_dir/keynet.onnx" ]]; then
  "$render_bin" --livekeytest
else
  echo "Neural key proof skipped: $model_dir/keynet.onnx is not installed"
fi

release_dir="$build_dir/SattariAutoPitch_artefacts/Release"
bundles=(
  "$release_dir/AU/Sattari Auto Pitch.component"
  "$release_dir/VST3/Sattari Auto Pitch.vst3"
  "$release_dir/Standalone/Sattari Auto Pitch.app"
)

"$repo_root/scripts/bundle_auto_pitch_runtime_deps.sh" "${bundles[@]}"
for bundle in "${bundles[@]}"; do
  "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$bundle"
done

echo "Auto Pitch neural release build passed: $release_dir"
echo "Models remain external until their redistribution rights and provenance are recorded."
