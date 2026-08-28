#!/usr/bin/env bash
# Assemble an internal, traceable Auto Pitch tester kit from the proven neural lane.
# CREPE-tiny is embedded; optional key models remain external and fingerprinted.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SATTARI_AUTOPITCH_BUILD_DIR:-$repo_root/build/auto-pitch-neural}"
out_root="${1:-$repo_root/build/tester-packages}"
model_dir="${SATTARI_AUTOPITCH_MODEL_DIR:-$HOME/Library/Sattari Audio/models}"

if [[ "${SATTARI_SKIP_AUTOPITCH_BUILD:-0}" != "1" ]]; then
  SATTARI_AUTOPITCH_BUILD_DIR="$build_dir" "$repo_root/scripts/build_auto_pitch_neural.sh"
fi

release_dir="$build_dir/SattariAutoPitch_artefacts/Release"
source_bundles=(
  "$release_dir/AU/Sattari Auto Pitch.component"
  "$release_dir/VST3/Sattari Auto Pitch.vst3"
  "$release_dir/Standalone/Sattari Auto Pitch.app"
)
for bundle in "${source_bundles[@]}"; do
  "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$bundle"
done

model_requirements=()
if [[ "${SATTARI_TESTER_ALLOW_MODEL_FALLBACK:-0}" != "1" ]]; then
  model_requirements=(
    --require keynet.onnx
    --require keynet_spec.onnx
  )
fi
if (( ${#model_requirements[@]} > 0 )); then
  "$repo_root/scripts/verify_auto_pitch_models.py" \
    --model-dir "$model_dir" "${model_requirements[@]}"
else
  # macOS still ships Bash 3.2, where expanding an empty array under `set -u`
  # raises "unbound variable". Keep the intentional fallback lane explicit.
  "$repo_root/scripts/verify_auto_pitch_models.py" --model-dir "$model_dir"
fi

version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
  "$release_dir/Standalone/Sattari Auto Pitch.app/Contents/Info.plist")"
package_name="sattari-auto-pitch-neural-tester-v$version"
package_dir="$out_root/$package_name"
archive="$out_root/$package_name.tar.gz"
mkdir -p "$out_root"
if [[ -e "$package_dir" || -e "$archive" ]]; then
  previous="$out_root/.previous-$package_name-$(date -u +%Y%m%dT%H%M%SZ)"
  mkdir -p "$previous"
  [[ ! -e "$package_dir" ]] || mv "$package_dir" "$previous/"
  [[ ! -e "$archive" ]] || mv "$archive" "$previous/"
  echo "previous tester output preserved at $previous"
fi

mkdir -p \
  "$package_dir/Plugins/Components" \
  "$package_dir/Plugins/VST3" \
  "$package_dir/Applications" \
  "$package_dir/Validation" \
  "$package_dir/Legal" \
  "$package_dir/Legal/Source-Tools" \
  "$package_dir/scripts"

ditto "${source_bundles[0]}" "$package_dir/Plugins/Components/Sattari Auto Pitch.component"
ditto "${source_bundles[1]}" "$package_dir/Plugins/VST3/Sattari Auto Pitch.vst3"
ditto "${source_bundles[2]}" "$package_dir/Applications/Sattari Auto Pitch.app"

packaged_bundles=(
  "$package_dir/Plugins/Components/Sattari Auto Pitch.component"
  "$package_dir/Plugins/VST3/Sattari Auto Pitch.vst3"
  "$package_dir/Applications/Sattari Auto Pitch.app"
)
for bundle in "${packaged_bundles[@]}"; do
  "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$bundle"
done

"$repo_root/scripts/verify_auto_pitch_models.py" \
  --model-dir "$model_dir" --json > "$package_dir/Validation/LOCAL-MODEL-FINGERPRINTS.json"
if [[ -f "$build_dir/validation/objective-benchmark.txt" ]]; then
  cp "$build_dir/validation/objective-benchmark.txt" \
    "$package_dir/Validation/OBJECTIVE-BENCHMARK.txt"
fi
cp "$repo_root/validation/auto-pitch/2026-08-09-objective-benchmark.md" \
  "$package_dir/Validation/OBJECTIVE-BENCHMARK-CURRENT-RUN.md"
cp "$repo_root/validation/auto-pitch/corpus-manifest.example.json" \
  "$package_dir/Validation/REAL-VOCAL-CORPUS-MANIFEST.example.json"
cp "$repo_root/scripts/qualify_auto_pitch_corpus.py" \
  "$package_dir/scripts/qualify_auto_pitch_corpus.py"
cp "$repo_root/LICENSE" "$package_dir/Legal/GPL-3.0.txt"
cp "$repo_root/LICENSING.md" "$package_dir/Legal/LICENSING.md"
cp "$repo_root/third_party/JUCE/LICENSE.md" "$package_dir/Legal/JUCE-LICENSE.md"
cp "$repo_root/plugins/auto-pitch/models/CREPE-LICENSE.txt" "$package_dir/Legal/CREPE-MIT-LICENSE.txt"
cp "$repo_root/plugins/auto-pitch/models/model-release.json" "$package_dir/Legal/CREPE-MODEL-PROVENANCE.json"
cp "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$package_dir/scripts/verify_auto_pitch_bundle.sh"
cp "$repo_root/scripts/verify_auto_pitch_models.py" "$package_dir/scripts/verify_auto_pitch_models.py"
cp "$repo_root/scripts/build_auto_pitch_neural.sh" "$package_dir/Legal/Source-Tools/"
cp "$repo_root/scripts/bundle_auto_pitch_runtime_deps.sh" "$package_dir/Legal/Source-Tools/"
cp "$repo_root/scripts/fetch_onnxruntime_macos.sh" "$package_dir/Legal/Source-Tools/"
cp "$repo_root/scripts/fetch_crepe_onnx.py" "$package_dir/Legal/Source-Tools/"
cp "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$package_dir/Legal/Source-Tools/"
cp "$repo_root/scripts/verify_auto_pitch_models.py" "$package_dir/Legal/Source-Tools/"
chmod +x "$package_dir/scripts/verify_auto_pitch_bundle.sh" \
  "$package_dir/scripts/verify_auto_pitch_models.py" \
  "$package_dir/scripts/qualify_auto_pitch_corpus.py"

source_commit="$(git -C "$repo_root" rev-parse HEAD)"
source_remote="$(git -C "$repo_root" remote get-url origin)"
git -C "$repo_root" diff --binary HEAD -- \
  CMakeLists.txt cmake core plugins/auto-pitch \
  > "$package_dir/Legal/WORKTREE.patch"
git -C "$repo_root" submodule status third_party/JUCE > "$package_dir/Legal/SUBMODULES.txt"
git -C "$repo_root" status --short -- \
  CMakeLists.txt cmake core plugins/auto-pitch \
  > "$package_dir/Legal/WORKTREE-STATUS.txt"

cat > "$package_dir/Legal/SOURCE-OFFER.txt" <<EOF
Sattari Audio complete corresponding source for this internal tester

Base commit: $source_commit
Source repository: $source_remote

Apply WORKTREE.patch to the base commit, restore the package-time helper scripts
from Source-Tools/, and initialize the exact submodules listed in SUBMODULES.txt.
WORKTREE-STATUS.txt records the scoped package-time working tree.
The binary-bearing Auto Pitch, core, CMake, and dependency sources must not contain
untracked files when this kit is assembled.
EOF

untracked_build_sources="$(git -C "$repo_root" ls-files --others --exclude-standard -- \
  CMakeLists.txt cmake core plugins/auto-pitch third_party/JUCE third_party/rubberband)"
if [[ -n "$untracked_build_sources" ]]; then
  echo "untracked binary-bearing source prevents a traceable tester package:" >&2
  echo "$untracked_build_sources" >&2
  exit 2
fi

cat > "$package_dir/README.md" <<EOF
# Sattari Auto Pitch neural tester v$version

Truth label: **internal DAW/listening candidate**. The AU, VST3, and standalone
bundles are universal macOS 13.3+ builds with pinned ONNX Runtime embedded and
verified. They are not Developer ID signed, notarized, or sale-ready.

## Install

Run \`./scripts/install_user.sh\`. Existing Sattari Auto Pitch bundles are backed
up before replacement. Restart or rescan the DAW afterward.

## Validate

Run \`./scripts/validate_local.sh\`. It verifies architecture, signatures, runtime
self-containment, pluginval when installed, and the installed Audio Unit via auval.

## Neural models

The original MIT CREPE-tiny pitch model is embedded and Neural Pitch works
without a separate install. For the optional neural-key test surface, place
locally authorized copies of \`keynet.onnx\` and \`keynet_spec.onnx\` in:

\`~/Library/Sattari Audio/models/\`

\`Validation/LOCAL-MODEL-FINGERPRINTS.json\` records what was tested on the build
machine; it is not a license or redistribution approval.

## Human gates

Test Logic and at least one VST3 host with real vocals, automation, sidechain key
input, session reload, multiple instances, low buffer sizes, and sample-rate changes.
Record artifacts, CPU spikes, pitch misses, and confusing controls before beta.
EOF

cat > "$package_dir/scripts/install_user.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
package_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
backup_root="$HOME/Library/Application Support/Sattari Audio/Backups/Auto Pitch/$(date -u +%Y%m%dT%H%M%SZ)"
sources=(
  "$package_root/Plugins/Components/Sattari Auto Pitch.component"
  "$package_root/Plugins/VST3/Sattari Auto Pitch.vst3"
  "$package_root/Applications/Sattari Auto Pitch.app"
)
destinations=(
  "$HOME/Library/Audio/Plug-Ins/Components/Sattari Auto Pitch.component"
  "$HOME/Library/Audio/Plug-Ins/VST3/Sattari Auto Pitch.vst3"
  "$HOME/Applications/Sattari Auto Pitch.app"
)
for index in 0 1 2; do
  source_bundle="${sources[$index]}"
  destination="${destinations[$index]}"
  mkdir -p "$(dirname "$destination")"
  if [[ -e "$destination" ]]; then
    mkdir -p "$backup_root"
    ditto "$destination" "$backup_root/$(basename "$destination")"
  fi
  ditto "$source_bundle" "$destination"
  echo "installed: $destination"
done
[[ ! -d "$backup_root" ]] || echo "previous bundles backed up at: $backup_root"
EOF

cat > "$package_dir/scripts/validate_local.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
package_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bundles=(
  "$package_root/Plugins/Components/Sattari Auto Pitch.component"
  "$package_root/Plugins/VST3/Sattari Auto Pitch.vst3"
  "$package_root/Applications/Sattari Auto Pitch.app"
)
for bundle in "${bundles[@]}"; do
  "$package_root/scripts/verify_auto_pitch_bundle.sh" "$bundle"
done
"$package_root/scripts/verify_auto_pitch_models.py"
if [[ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
  /Applications/pluginval.app/Contents/MacOS/pluginval \
    --validate-in-process --strictness-level 5 "${bundles[1]}"
else
  echo "pluginval not found; VST3 host validation skipped"
fi
if command -v auval >/dev/null 2>&1; then
  auval -v aufx Ap01 Satr
fi
EOF
chmod +x "$package_dir/scripts/install_user.sh" "$package_dir/scripts/validate_local.sh"

cat > "$package_dir/MANIFEST.txt" <<EOF
package: $package_name
product: Sattari Auto Pitch
version: $version
truth_label: internal DAW/listening candidate; not notarized; not sale-ready
minimum_macos: 13.3
architectures: arm64,x86_64
formats: AU,VST3,Standalone
models_included: no
created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)
source_commit: $source_commit
source_repository: $source_remote
EOF

(
  cd "$package_dir"
  find Plugins Applications Validation Legal scripts -type f -print0 \
    | sort -z \
    | xargs -0 shasum -a 256 > SHA256SUMS.txt
)
(
  cd "$out_root"
  tar -czf "$package_name.tar.gz" "$package_name"
)

echo "Auto Pitch neural tester packaged: $package_dir"
echo "Archive: $archive"
