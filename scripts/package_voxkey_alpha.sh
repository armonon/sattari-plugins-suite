#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$repo_root/../.." && pwd)"
out_root="${1:-$workspace_root/deliverables/sattari-test-builds}"
version="${VOXKEY_VERSION:-0.1.0-ableton-mvp}"
package_dir="$out_root/voxkey-plugin-alpha-v$version"
build_dir="${BUILD_DIR:-$repo_root/build/voxkey}"
release_dir="$build_dir/SattariVoxKey_artefacts/Release"
fallback_release_dir="$build_dir/SattariVoxKey_artefacts"

cd "$repo_root"
mkdir -p "$out_root"
./scripts/validate_voxkey.sh

rm -rf "$package_dir"
mkdir -p "$package_dir/build-artefacts" "$package_dir/scripts" "$package_dir/docs"

if [[ -d "$release_dir/VST3/Sattari VoxKey.vst3" ]]; then
  cp -R "$release_dir/VST3/Sattari VoxKey.vst3" "$package_dir/build-artefacts/"
elif [[ -d "$fallback_release_dir/VST3/Sattari VoxKey.vst3" ]]; then
  cp -R "$fallback_release_dir/VST3/Sattari VoxKey.vst3" "$package_dir/build-artefacts/"
fi

if [[ -d "$release_dir/AU/Sattari VoxKey.component" ]]; then
  cp -R "$release_dir/AU/Sattari VoxKey.component" "$package_dir/build-artefacts/"
elif [[ -d "$fallback_release_dir/AU/Sattari VoxKey.component" ]]; then
  cp -R "$fallback_release_dir/AU/Sattari VoxKey.component" "$package_dir/build-artefacts/"
fi

cp scripts/validate_voxkey.sh "$package_dir/scripts/"
cp plugins/voxkey/README.md "$package_dir/README.md"
if [[ -f plugins/voxkey/docs/voxkey-architecture.md ]]; then
  cp plugins/voxkey/docs/voxkey-architecture.md "$package_dir/docs/"
fi

cat > "$package_dir/INSTALL.md" <<'INSTALL'
# Sattari VoxKey alpha install

Truth label: internal MVP alpha. Not sale-ready.

## macOS VST3

Copy:

```text
build-artefacts/Sattari VoxKey.vst3
```

to:

```text
~/Library/Audio/Plug-Ins/VST3/
```

Then rescan plugins in Ableton Live.

## Ableton routing

1. Audio Track: microphone input + Sattari VoxKey inserted.
2. MIDI Track: software instrument loaded.
3. MIDI Track `MIDI From`: select the audio track / Sattari VoxKey plugin output.
4. Monitor the audio and MIDI tracks.
5. Sing to hear dry voice plus generated MIDI instrument.
INSTALL

cat > "$package_dir/MANIFEST.txt" <<MANIFEST
Sattari VoxKey alpha package
version=$version
truth_label=internal MVP alpha; not sale-ready
contains=VST3/AU if built, validation script, Ableton setup docs
MANIFEST

(
  cd "$out_root"
  tar -czf "$(basename "$package_dir").tar.gz" "$(basename "$package_dir")"
)

shasum -a 256 "$package_dir.tar.gz" > "$package_dir.sha256"
echo "packaged $package_dir"
echo "archive $package_dir.tar.gz"
cat "$package_dir.sha256"
