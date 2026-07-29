#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! git -C "$repo_root" diff --quiet --ignore-submodules=all \
   || ! git -C "$repo_root" diff --cached --quiet --ignore-submodules=all; then
  echo "tracked source is dirty; commit the exact source before packaging binaries" >&2
  exit 1
fi
workspace_root="$(cd "$repo_root/../.." && pwd)"
hub_root="$repo_root/apps/sattari-hub"
out_root="${1:-$workspace_root/deliverables/sattari-test-builds}"
version="0.1.2"
package_dir="$out_root/sattari-hub-alpha-v$version"
app_dir="$package_dir/Sattari Hub.app"
contents_dir="$app_dir/Contents"
macos_dir="$contents_dir/MacOS"
resources_dir="$contents_dir/Resources"

mkdir -p "$out_root"
rm -rf "$package_dir"
mkdir -p "$macos_dir" "$resources_dir"

swift build --package-path "$hub_root" -c release
binary="$hub_root/.build/release/SattariHub"
if [[ ! -x "$binary" ]]; then
  echo "missing built SattariHub binary: $binary" >&2
  exit 1
fi

cp "$binary" "$macos_dir/SattariHub"
cp "$hub_root/Packaging/Info.plist" "$contents_dir/Info.plist"
cp "$hub_root/Sources/SattariHub/Resources/sattari-products.json" "$resources_dir/sattari-products.json"

payload_dir="$resources_dir/Payload"
mkdir -p "$payload_dir"
validate_bundle() {
  local bundle="$1"
  [[ -d "$bundle" ]] || { echo "invalid payload (not a bundle directory): $bundle" >&2; return 1; }
  [[ -f "$bundle/Contents/Info.plist" ]] || { echo "invalid payload (missing Info.plist): $bundle" >&2; return 1; }
  local executable
  executable="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f -perm -111 -print -quit 2>/dev/null)"
  [[ -n "$executable" ]] || { echo "invalid payload (missing executable): $bundle" >&2; return 1; }
}

copied=0
while IFS= read -r relative_path; do
  [[ -n "$relative_path" ]] || continue
  source_path="$repo_root/$relative_path"
  validate_bundle "$source_path"
  mkdir -p "$payload_dir/$(dirname "$relative_path")"
  ditto "$source_path" "$payload_dir/$relative_path"
  validate_bundle "$payload_dir/$relative_path"
  copied=$((copied + 1))
done < <(python3 - <<'PY' "$hub_root/Sources/SattariHub/Resources/sattari-products.json"
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
seen = set()
for product in manifest['products']:
    if product.get('shippingProfile') != 'alpha':
        continue
    for bundle in product['bundles']:
        rel = bundle['relativeSourcePath']
        if rel not in seen:
            seen.add(rel)
            print(rel)
PY
)
[[ "$copied" -eq 9 ]] || { echo "expected 9 complete alpha bundles, copied $copied" >&2; exit 1; }

mkdir -p "$resources_dir/Legal"
cp "$repo_root/LICENSE" "$resources_dir/Legal/GPL-3.0.txt"
cp "$repo_root/LICENSING.md" "$resources_dir/Legal/LICENSING.md"
cp "$repo_root/third_party/JUCE/LICENSE.md" "$resources_dir/Legal/JUCE-LICENSE.md"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
source_remote="$(git -C "$repo_root" remote get-url origin)"
cat > "$resources_dir/Legal/SOURCE-OFFER.txt" <<EOF
Sattari Audio complete corresponding source

The binaries in this package were built from commit:
$source_commit

Source repository:
$source_remote

The repository at that commit, including its pinned submodules and build scripts,
is the complete corresponding source under GPLv3. A source copy is available at
no charge. See GPL-3.0.txt and LICENSING.md in this directory.
EOF

cat > "$package_dir/README.md" <<EOF
# Sattari Hub alpha v$version

Truth label: **internal alpha installer/updater app**. This is not notarized, not sale-ready, and not a public download.

## What this is

Sattari Hub is a downloadable Mac app foundation for installing, updating, and hosting all Sattari Audio software from one place.

## Current update channel

Bundled/internal alpha. The app reads its bundled product manifest and first installs from payloads embedded inside \`Sattari Hub.app/Contents/Resources/Payload\`. If you launch it from the repo or set \`SATTARI_REPO_ROOT\`, it can also use fresh local builds as an update source.

## Current installs

When products are available, Hub copies:

- AU components to \`~/Library/Audio/Plug-Ins/Components\`
- VST3 plugins to \`~/Library/Audio/Plug-Ins/VST3\`
- Standalone apps to \`~/Applications\`

Existing bundles are backed up under \`~/Library/Application Support/Sattari Hub/Backups\` before replacement. Install receipts are written to \`~/Library/Application Support/Sattari Hub/receipts.json\`.

## Next gates

- Bundle plugin packages inside the Hub DMG or connect a signed update feed.
- Add Sparkle-style app self-update and per-product download/update checks.
- Add uninstall/repair controls.
- Add Developer ID signing + notarized DMG before any public distribution.
EOF

cat > "$package_dir/MANIFEST.txt" <<EOF
package: sattari-hub-alpha-v$version
product: Sattari Hub
version: $version
truth_label: internal alpha installer/updater app; not sale-ready; not notarized
created_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)
source_repo: $repo_root
app: Sattari Hub.app
manifest: Sattari Hub.app/Contents/Resources/sattari-products.json
payload_bundles: $copied
source_commit: $source_commit
source_repository: $source_remote
EOF

codesign --force --deep --sign - "$app_dir" >/dev/null
codesign --verify --deep --strict --verbose=2 "$app_dir"

(
  cd "$out_root"
  tar -czf "sattari-hub-alpha-v$version.tar.gz" "sattari-hub-alpha-v$version"
)

echo "packaged $package_dir"
