#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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
while IFS= read -r relative_path; do
  [[ -n "$relative_path" ]] || continue
  source_path="$repo_root/$relative_path"
  if [[ -e "$source_path" ]]; then
    mkdir -p "$payload_dir/$(dirname "$relative_path")"
    ditto "$source_path" "$payload_dir/$relative_path"
  else
    echo "warning: missing payload source, Hub will skip on other machines: $relative_path" >&2
  fi
done < <(python3 - <<'PY' "$hub_root/Sources/SattariHub/Resources/sattari-products.json"
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
seen = set()
for product in manifest['products']:
    for bundle in product['bundles']:
        rel = bundle['relativeSourcePath']
        if rel not in seen:
            seen.add(rel)
            print(rel)
PY
)

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
EOF

codesign --force --deep --sign - "$app_dir" >/dev/null
codesign --verify --deep --strict --verbose=2 "$app_dir"

(
  cd "$out_root"
  tar -czf "sattari-hub-alpha-v$version.tar.gz" "sattari-hub-alpha-v$version"
)

echo "packaged $package_dir"
