#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$repo_root/../.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build/stemdeck}"
out_root="${1:-$workspace_root/deliverables/sattari-test-builds}"
version="${STEMDECK_VERSION:-0.5.0-studio-workflow}"
package_dir="$out_root/stemdeck-plugin-alpha-v$version"
release_dir="$build_dir/SattariStemDeck_artefacts/Release"

cd "$repo_root"

mkdir -p "$out_root"
./scripts/validate_stemdeck.sh

rm -rf "$package_dir"
mkdir -p "$package_dir/build-artefacts" "$package_dir/scripts" "$package_dir/source" "$package_dir/docs" "$package_dir/validation"

if [[ -d "$release_dir/VST3/Sattari StemDeck.vst3" ]]; then
  ditto "$release_dir/VST3/Sattari StemDeck.vst3" "$package_dir/build-artefacts/Sattari StemDeck.vst3"
fi

if [[ -d "$release_dir/Standalone/Sattari StemDeck.app" ]]; then
  ditto "$release_dir/Standalone/Sattari StemDeck.app" "$package_dir/build-artefacts/Sattari StemDeck.app"
  codesign --force --deep --sign - "$package_dir/build-artefacts/Sattari StemDeck.app" >/dev/null
fi

ditto "$repo_root/plugins/stemdeck" "$package_dir/source/stemdeck"
ditto "$repo_root/scripts/validate_stemdeck.sh" "$package_dir/scripts/validate_stemdeck.sh"
ditto "$repo_root/scripts/stemdeck_manual_validation_template.sh" "$package_dir/scripts/stemdeck_manual_validation_template.sh"
ditto "$repo_root/scripts/setup_stemdeck_demucs.sh" "$package_dir/scripts/setup_stemdeck_demucs.sh"
ditto "$repo_root/scripts/stemdeck_separate_demucs.py" "$package_dir/scripts/stemdeck_separate_demucs.py"
ditto "$repo_root/scripts/stemdeck_separate_demucs_venv.sh" "$package_dir/scripts/stemdeck_separate_demucs_venv.sh"
if [[ -d "$build_dir/timepitch-benchmark" ]]; then
  ditto "$build_dir/timepitch-benchmark" "$package_dir/validation/timepitch-benchmark"
fi

cat > "$package_dir/scripts/install_user_plugins.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
pkg_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"
if [[ -d "$pkg_root/build-artefacts/Sattari StemDeck.vst3" ]]; then
  ditto "$pkg_root/build-artefacts/Sattari StemDeck.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3"
fi
if [[ -d "$pkg_root/build-artefacts/Sattari StemDeck.app" ]]; then
  ditto "$pkg_root/build-artefacts/Sattari StemDeck.app" "$HOME/Applications/Sattari StemDeck.app"
fi
INSTALL

cat > "$package_dir/scripts/validate_local.sh" <<'VALIDATE'
#!/usr/bin/env bash
set -euo pipefail
pkg_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for item in "$pkg_root"/build-artefacts/*.vst3 "$pkg_root"/build-artefacts/*.app; do
  [[ -e "$item" ]] || continue
  codesign --verify --deep --strict --verbose=2 "$item"
done
if [[ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
  /Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "$pkg_root/build-artefacts/Sattari StemDeck.vst3"
else
  echo "pluginval not found; skipping VST3 pluginval."
fi
VALIDATE

chmod +x "$package_dir/scripts/"*.sh

cat > "$package_dir/README.md" <<EOF_README
# Sattari StemDeck alpha v$version

Truth label: **internal alpha validation package**. This package is not sale-ready and must not be represented as a finished product.

## Included

- VST3 and Standalone app build artefacts.
- Source snapshot for \`plugins/stemdeck\`.
- Local install helper: \`scripts/install_user_plugins.sh\`.
- Local validation helper: \`scripts/validate_local.sh\`.
- Optional local Demucs setup helper: \`scripts/setup_stemdeck_demucs.sh\`.
- Split DJ/DAW UI direction: top live mixer/stem decks, lower open arrangement recorder lanes for capturing the performed mix.
- Premium AI stem UX: per-deck AI Split trigger/status, cache-aware stem loading, and automatic Full Mix mute when all four generated stems are ready.
- DAW/listening checklist: \`docs/stemdeck-sale-readiness-gates.md\`.
- Manual validation report generator: \`scripts/stemdeck_manual_validation_template.sh\`.
- Time/pitch benchmark listening pack: \`validation/timepitch-benchmark/\`.

## Current feature truth

- Four default decks (expandable to eight channels), pads, mic monitor, master recording, per-channel capture, host BPM sync, global master BPM/key, beat/grid sync, and limiter are present.
- External stem-lane loading exists for pre-separated Full/Drums/Bass/Music/Vocals files per deck, sharing one deck transport/playhead.
- Offline-first cached stem loading has a manifest/provider seam plus optional Demucs command integration. Demucs is not bundled in the plugin/app; run \`scripts/setup_stemdeck_demucs.sh\` on a local machine to create the ignored venv and write StemDeck's separator config.
- WarpEngine centralizes per-deck BPM/grid/downbeat/sync-mode/pitch/key-lock decisions and can compile against Rubber Band when available; machines without Rubber Band use the realtime granular fallback.
- The Preserve control enables pitch-preserving tempo/key moves through Rubber Band when linked, otherwise through the internal granular fallback. The validation gate now emits a synthetic time/pitch benchmark WAV pack and manifest for listening review, but real-session DAW/listening validation is still required before any sale-candidate claim.
- LiveKey can periodically analyze the current full-mix playback window and use that estimate for target-key matching/shifting, with load-time analysis as fallback. This is alpha-quality and needs real-song listening validation.
- Beat Sync uses detected/user BPM, beat grid/downbeat correction, transient anchors, draggable multi-marker warping, tempo interpretation, and Strict/Groove/Anchor/Free Drift sync modes. Real-song stress testing is still required.
- Native editor now presents the DJ app as a split performance workspace: four live mixer/stem decks across the top, master/crossfader/record controls in the middle, and an open arrangement recorder at the bottom with lanes for Master Mix, decks, stem moves, FX/pads, and mic/voice.
- Performance recording writes master, mic, per-deck render files, event-log JSON, and arrangement JSON v2 with deck/mic/master lanes, automation lanes, and an Add New Track placeholder.
- The effects rack scans, browses, and hosts inserts on master, mic, channel, and stem destinations; third-party processing uses the helper-process sandbox. Plugin compatibility still needs broad vendor testing.
- Manual DAW and real-song listening validation are still required.
EOF_README

cat > "$package_dir/docs/stemdeck-sale-readiness-gates.md" < "$repo_root/plugins/stemdeck/docs/stemdeck-sale-readiness-gates.md"

{
  echo "package: $(basename "$package_dir")"
  echo "product: Sattari StemDeck"
  echo "version: $version"
  echo "truth_label: internal alpha validation package; not sale-ready"
  echo "created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "source_commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "source_repo: $repo_root"
  echo "build_dir: $build_dir"
  echo "formats: VST3, Standalone"
  echo "artifacts:"
  find "$package_dir/build-artefacts" -maxdepth 1 -mindepth 1 -print | sed "s#^$package_dir/#  - #"
} > "$package_dir/MANIFEST.txt"

(cd "$out_root" && tar -czf "$(basename "$package_dir").tar.gz" "$(basename "$package_dir")")

echo "packaged $package_dir"
echo "archive $package_dir.tar.gz"
