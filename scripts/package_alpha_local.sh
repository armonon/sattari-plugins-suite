#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$repo_root/../.." && pwd)"
out_root="${1:-$workspace_root/deliverables/sattari-test-builds}"
build_root="${SATTARI_BUILD_ROOT:-$repo_root/build/all/plugins}"
version="0.1.0"

mkdir -p "$out_root"

write_file() {
  local path="$1"
  shift
  mkdir -p "$(dirname "$path")"
  cat > "$path" <<EOF
$*
EOF
}

package_one() {
  local slug="$1" target product bundle_dir au_name vst3_name app_name au_type au_subtype formats limitation

  case "$slug" in
    sub-conjurer)
      target="SubConjurer"; product="Sub Conjurer"; bundle_dir="SubConjurer_artefacts";
      au_name="Sub Conjurer.component"; vst3_name="Sub Conjurer.vst3"; app_name="Sub Conjurer.app";
      au_type="aumf"; au_subtype="Sc01"; formats="AU, VST3, Standalone";
      limitation="Needs DAW/manual listening validation on real kick/bass material and final DSP tuning before any sale-candidate claim."
      ;;
    stemdeck)
      target="SattariStemDeck"; product="Sattari StemDeck"; bundle_dir="SattariStemDeck_artefacts";
      au_name=""; vst3_name="Sattari StemDeck.vst3"; app_name="Sattari StemDeck.app";
      au_type=""; au_subtype=""; formats="VST3, Standalone";
      limitation="No AU build yet; no AI stem separation, beatgrid, time-stretching, or long-set streaming; needs DAW/manual file-loading and listening validation."
      ;;
    side-chain-master)
      target="SideChainMaster"; product="Side Chain Master"; bundle_dir="SideChainMaster_artefacts";
      au_name="Side Chain Master.component"; vst3_name="Side Chain Master.vst3"; app_name="Side Chain Master.app";
      au_type="aufx"; au_subtype="Scm1"; formats="AU, VST3, Standalone";
      limitation="Needs Logic/Ableton/FL sidechain routing validation, gain-reduction behavior checks, and listening notes on real sessions."
      ;;
    royal-chain)
      target="SattariRoyalChain"; product="Sattari Royal Chain"; bundle_dir="SattariRoyalChain_artefacts";
      au_name="Sattari Royal Chain.component"; vst3_name="Sattari Royal Chain.vst3"; app_name="Sattari Royal Chain.app";
      au_type="aufx"; au_subtype="Rc01"; formats="AU, VST3, Standalone";
      limitation="Vocal Tune module is visibly labeled as a disabled placeholder in Lite; needs real-vocal DAW/listening validation and preset/product UX work."
      ;;
    auto-pitch)
      target="SattariAutoPitch"; product="Sattari Auto Pitch"; bundle_dir="SattariAutoPitch_artefacts";
      au_name="Sattari Auto Pitch.component"; vst3_name="Sattari Auto Pitch.vst3"; app_name="Sattari Auto Pitch.app";
      au_type="aufx"; au_subtype="Ap01"; formats="AU, VST3, Standalone";
      limitation="Key detector/plugin shell exists as an internal alpha; real-vocal tuning quality, Adapt Mode render quality, and DAW/manual listening gates are not passed yet."
      ;;
    sattari-arp)
      target="SattariArp"; product="Sattari Arp"; bundle_dir="SattariArp_artefacts";
      au_name="Sattari Arp.component"; vst3_name="Sattari Arp.vst3"; app_name="";
      au_type=""; au_subtype=""; formats="AU, VST3";
      limitation="MIDI-effect internal alpha; needs DAW MIDI routing, preset UX, timing feel, and generated-pattern validation before any product claim."
      ;;
    harmony)
      target="SattariHarmony"; product="Sattari Harmony"; bundle_dir="SattariHarmony_artefacts";
      au_name="Sattari Harmony.component"; vst3_name="Sattari Harmony.vst3"; app_name="Sattari Harmony.app";
      au_type="aufx"; au_subtype="Hm01"; formats="AU, VST3, Standalone";
      limitation="Diatonic/chord-follow harmoniser + choir; render proof confirms the 5th is generated, but real-vocal listening and per-voice quality tuning are not validated yet."
      ;;
    maqam)
      target="SattariMaqam"; product="Sattari Maqam"; bundle_dir="SattariMaqam_artefacts";
      au_name="Sattari Maqam.component"; vst3_name="Sattari Maqam.vst3"; app_name="Sattari Maqam.app";
      au_type="aufx"; au_subtype="Mq01"; formats="AU, VST3, Standalone";
      limitation="Microtonal maqam tuner; render proof confirms quarter-tone snapping to 0.02c, but the maqam cents tables and real-vocal feel need musician validation."
      ;;
    vocal-master)
      target="SattariVocalMaster"; product="Sattari Vocal Master"; bundle_dir="SattariVocalMaster_artefacts";
      au_name="Sattari Vocal Master.component"; vst3_name="Sattari Vocal Master.vst3"; app_name="Sattari Vocal Master.app";
      au_type="aufx"; au_subtype="Vm01"; formats="AU, VST3, Standalone";
      limitation="Vocal cleanup chain; render proof confirms mid-side isolate, but de-noise/de-ess thresholds and the offline AI stem-removal path need real-material validation."
      ;;
    rhythm-genie)
      target="SattariRhythmGenie"; product="Sattari Rhythm Genie"; bundle_dir="SattariRhythmGenie_artefacts";
      au_name="Sattari Rhythm Genie.component"; vst3_name="Sattari Rhythm Genie.vst3"; app_name="Sattari Rhythm Genie.app";
      au_type="aumf"; au_subtype="Rg01"; formats="AU, VST3, Standalone";
      limitation="Host-synced MIDI drum generator; render proof confirms step timing, but pattern musicality and DAW MIDI routing across hosts need validation."
      ;;
    song-key)
      target="SattariSongKey"; product="Sattari Key"; bundle_dir="SattariSongKey_artefacts";
      au_name="Sattari Key.component"; vst3_name="Sattari Key.vst3"; app_name="Sattari Key.app";
      au_type="aufx"; au_subtype="Sk01"; formats="AU, VST3, Standalone";
      limitation="Master key scanner that publishes to the suite bus; render proof confirms C-major detection, but key accuracy across real mixes and the publish workflow need validation."
      ;;
    *)
      echo "unknown plugin slug: $slug" >&2
      return 1
      ;;
  esac

  local package_dir="$out_root/${slug}-plugin-alpha-v${version}"
  local release_dir="$build_root/$slug/$bundle_dir/Release"

  rm -rf "$package_dir"
  mkdir -p "$package_dir/build-artefacts" "$package_dir/scripts" "$package_dir/source"

  if [[ -n "$au_name" && -d "$release_dir/AU/$au_name" ]]; then
    ditto "$release_dir/AU/$au_name" "$package_dir/build-artefacts/$au_name"
  fi
  if [[ -n "$vst3_name" && -d "$release_dir/VST3/$vst3_name" ]]; then
    ditto "$release_dir/VST3/$vst3_name" "$package_dir/build-artefacts/$vst3_name"
  fi
  if [[ -n "$app_name" && -d "$release_dir/Standalone/$app_name" ]]; then
    ditto "$release_dir/Standalone/$app_name" "$package_dir/build-artefacts/$app_name"
    # JUCE standalone app bundles from the raw CMake build can have stale ad-hoc
    # signatures; refresh only the packaged copy so local validation is stable.
    codesign --force --deep --sign - "$package_dir/build-artefacts/$app_name" >/dev/null
  fi
  ditto "$repo_root/plugins/$slug" "$package_dir/source/$slug"

  cat > "$package_dir/README.md" <<EOF
# $product alpha v$version

Truth label: **internal plugin build / alpha validation package**. This is not sale-ready and should not be marketed as a finished product.

## Formats

$formats

## Included

- Built plugin/app bundles in \`build-artefacts/\`.
- Source snapshot in \`source/$slug/\`.
- Local install helper: \`scripts/install_user_plugins.sh\`.
- Local validation helper: \`scripts/validate_local.sh\`.
- Package manifest: \`MANIFEST.txt\`.

## Current limitation

$limitation

## Next alpha gate

Install locally, open in target DAWs, run a short real-session listening pass, and record crash/CPU/artifact/UX notes before calling this a DAW-validated candidate.
EOF

  cat > "$package_dir/BUILD.md" <<EOF
# Build + validation commands

Source repo path at package time:

\`$repo_root\`

Commands used for the all-plugin alpha build gate:

\`\`\`bash
./scripts/validate_sources.sh
cmake -S . -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/all --config Release -j 4
\`\`\`

Product-specific source build:

\`\`\`bash
cmake -S plugins/$slug -B build/$slug -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/$slug --config Release -j 4
\`\`\`
EOF

  cat > "$package_dir/scripts/install_user_plugins.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
pkg_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"
shopt -s nullglob
for component in "$pkg_root"/build-artefacts/*.component; do
  echo "Installing AU: $(basename "$component")"
  ditto "$component" "$HOME/Library/Audio/Plug-Ins/Components/$(basename "$component")"
done
for vst3 in "$pkg_root"/build-artefacts/*.vst3; do
  echo "Installing VST3: $(basename "$vst3")"
  ditto "$vst3" "$HOME/Library/Audio/Plug-Ins/VST3/$(basename "$vst3")"
done
for app in "$pkg_root"/build-artefacts/*.app; do
  echo "Installing app: $(basename "$app")"
  ditto "$app" "$HOME/Applications/$(basename "$app")"
done
EOF

  cat > "$package_dir/scripts/validate_local.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
pkg_root="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")/.." && pwd)"

for item in "\$pkg_root"/build-artefacts/*.component "\$pkg_root"/build-artefacts/*.vst3 "\$pkg_root"/build-artefacts/*.app; do
  [[ -e "\$item" ]] || continue
  echo "--- codesign verify: \$item"
  codesign --verify --deep --strict --verbose=2 "\$item"
done

if [[ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
  for vst3 in "\$pkg_root"/build-artefacts/*.vst3; do
    [[ -e "\$vst3" ]] || continue
    echo "--- pluginval strictness 5: \$vst3"
    /Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "\$vst3"
  done
else
  echo "pluginval not found at /Applications/pluginval.app/Contents/MacOS/pluginval; skipping VST3 pluginval."
fi
EOF

  if [[ -n "$au_type" ]]; then
    cat >> "$package_dir/scripts/validate_local.sh" <<EOF

if command -v auval >/dev/null 2>&1; then
  echo "--- auval installed AU: auval -v $au_type $au_subtype Satr"
  echo "Note: auval validates the installed Audio Unit registered with macOS, not the package path. Run scripts/install_user_plugins.sh first if needed."
  auval -v $au_type $au_subtype Satr
fi
EOF
  fi

  chmod +x "$package_dir/scripts/install_user_plugins.sh" "$package_dir/scripts/validate_local.sh"

  {
    echo "package: $(basename "$package_dir")"
    echo "product: $product"
    echo "version: $version"
    echo "truth_label: internal plugin build / alpha validation package; not sale-ready"
    echo "created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "source_repo: $repo_root"
    echo "source_snapshot: source/$slug"
    echo "build_root: $build_root/$slug"
    echo "formats: $formats"
    echo "artifacts:"
    find "$package_dir/build-artefacts" -maxdepth 1 -mindepth 1 -print | sed "s#^$package_dir/#  - #"
  } > "$package_dir/MANIFEST.txt"

  (cd "$out_root" && tar -czf "$(basename "$package_dir").tar.gz" "$(basename "$package_dir")")
  echo "packaged $package_dir"
}

package_suite() {
  local suite_dir="$out_root/sattari-audio-plugin-suite-alpha-v${version}"
  rm -rf "$suite_dir"
  mkdir -p "$suite_dir/Plugins/Components" "$suite_dir/Plugins/VST3" "$suite_dir/Applications" "$suite_dir/Sources" "$suite_dir/scripts"

  for slug in sub-conjurer stemdeck side-chain-master royal-chain auto-pitch sattari-arp; do
    local package_dir="$out_root/${slug}-plugin-alpha-v${version}"
    if [[ ! -d "$package_dir" ]]; then
      echo "missing individual package for suite: $package_dir" >&2
      return 1
    fi
    shopt -s nullglob
    for component in "$package_dir"/build-artefacts/*.component; do
      ditto "$component" "$suite_dir/Plugins/Components/$(basename "$component")"
    done
    for vst3 in "$package_dir"/build-artefacts/*.vst3; do
      ditto "$vst3" "$suite_dir/Plugins/VST3/$(basename "$vst3")"
    done
    for app in "$package_dir"/build-artefacts/*.app; do
      ditto "$app" "$suite_dir/Applications/$(basename "$app")"
    done
    ditto "$package_dir/source/$slug" "$suite_dir/Sources/$slug"
  done

  cat > "$suite_dir/README.md" <<EOF
# Sattari Audio Plugin Suite alpha v$version

Truth label: **internal all-plugin suite bundle / alpha validation package**. This is for local install and validation only. It is not sale-ready, not a paid/public release, and not a claim that every plugin has passed DAW/manual listening gates.

## Included plugins

- Sattari Auto Pitch — AU, VST3, Standalone
- Sattari StemDeck — VST3, Standalone
- Sattari Arp — AU, VST3
- Sub Conjurer — AU, VST3, Standalone
- Side Chain Master — AU, VST3, Standalone
- Sattari Royal Chain — AU, VST3, Standalone

## Install locally

Run:

\`\`\`bash
./scripts/install_suite_user_plugins.sh
\`\`\`

This copies AUs to \`~/Library/Audio/Plug-Ins/Components\`, VST3s to \`~/Library/Audio/Plug-Ins/VST3\`, and standalone apps to \`~/Applications\`.

## Validate locally

Run:

\`\`\`bash
./scripts/validate_suite_local.sh
\`\`\`

If pluginval is installed at \`/Applications/pluginval.app/Contents/MacOS/pluginval\`, the helper also runs strictness-5 validation on VST3 bundles.

## Release gate reminder

Before any public/beta/sale-ready claim, each plugin still needs product-specific DAW open tests, real-session listening, crash/CPU checks, install/uninstall checks, preset UX review, and honest limitation notes.
EOF

  cat > "$suite_dir/scripts/install_suite_user_plugins.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
suite_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"
shopt -s nullglob
for component in "$suite_root"/Plugins/Components/*.component; do
  echo "Installing AU: $(basename "$component")"
  ditto "$component" "$HOME/Library/Audio/Plug-Ins/Components/$(basename "$component")"
done
for vst3 in "$suite_root"/Plugins/VST3/*.vst3; do
  echo "Installing VST3: $(basename "$vst3")"
  ditto "$vst3" "$HOME/Library/Audio/Plug-Ins/VST3/$(basename "$vst3")"
done
for app in "$suite_root"/Applications/*.app; do
  echo "Installing app: $(basename "$app")"
  ditto "$app" "$HOME/Applications/$(basename "$app")"
done
EOF

  cat > "$suite_dir/scripts/validate_suite_local.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
suite_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shopt -s nullglob
for item in "$suite_root"/Plugins/Components/*.component "$suite_root"/Plugins/VST3/*.vst3 "$suite_root"/Applications/*.app; do
  [[ -e "$item" ]] || continue
  echo "--- codesign verify: $item"
  codesign --verify --deep --strict --verbose=2 "$item"
done
if [[ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]]; then
  for vst3 in "$suite_root"/Plugins/VST3/*.vst3; do
    [[ -e "$vst3" ]] || continue
    echo "--- pluginval strictness 5: $vst3"
    /Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "$vst3"
  done
else
  echo "pluginval not found at /Applications/pluginval.app/Contents/MacOS/pluginval; skipping VST3 pluginval."
fi
EOF
  chmod +x "$suite_dir/scripts/install_suite_user_plugins.sh" "$suite_dir/scripts/validate_suite_local.sh"

  {
    echo "package: $(basename "$suite_dir")"
    echo "version: $version"
    echo "truth_label: internal all-plugin suite bundle / alpha validation package; not sale-ready"
    echo "created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "source_repo: $repo_root"
    echo "plugins:"
    find "$suite_dir/Plugins/Components" "$suite_dir/Plugins/VST3" "$suite_dir/Applications" -maxdepth 1 -mindepth 1 -print | sed "s#^$suite_dir/#  - #"
  } > "$suite_dir/MANIFEST.txt"

  (cd "$out_root" && tar -czf "$(basename "$suite_dir").tar.gz" "$(basename "$suite_dir")")
  echo "packaged $suite_dir"
}

# Slugs to package (override with SATTARI_PACKAGE_SLUGS="harmony maqam ..." to do a subset).
slugs="${SATTARI_PACKAGE_SLUGS:-sub-conjurer stemdeck side-chain-master royal-chain auto-pitch sattari-arp harmony maqam vocal-master rhythm-genie song-key}"
for slug in $slugs; do
  package_one "$slug"
done

# Suite bundle (skip with SATTARI_PACKAGE_SUITE=0 when packaging only a subset).
if [[ "${SATTARI_PACKAGE_SUITE:-1}" == "1" ]]; then
  package_suite
fi
