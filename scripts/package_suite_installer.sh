#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio Suite - unified macOS .pkg installer
# -----------------------------------------------------------------------------
#  Produces a single double-clickable installer that drops the Sattari Hub.app
#  into /Applications. The Hub bundles every available plugin payload inside
#  itself; once installed, the user opens the Hub and installs the curated alpha
#  set (Brain, Arp and Sub Conjurer) into their user plug-in folders.
#
#  Usage:
#     scripts/package_suite_installer.sh [output-dir]
#
#  Default output: <repo>/build/installer/Sattari-Audio-Suite.pkg
#
#  Prerequisites (all standard macOS tooling):
#   * Plugin bundles in build/all/plugins/.../Release/{AU,VST3,Standalone}/
#   * Swift toolchain (for the Hub Swift app build)
#   * /usr/bin/pkgbuild  (system; always present)
#
#  The installer is ad-hoc signed. For distribution to testers without
#  Gatekeeper warnings, sign + notarize the produced .pkg separately:
#     productsign --sign "Developer ID Installer: ..." in.pkg out.pkg
#     xcrun notarytool submit out.pkg --keychain-profile <...> --wait
# =============================================================================
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! git -C "$repo_root" diff --quiet --ignore-submodules=all \
   || ! git -C "$repo_root" diff --cached --quiet --ignore-submodules=all; then
    echo "tracked source is dirty; commit the exact source before packaging binaries" >&2
    exit 2
fi
out_dir="${1:-$repo_root/build/installer}"
mkdir -p "$out_dir"

stage_root="$out_dir/_stage"
payload_root="$stage_root/Applications"
hub_app="$payload_root/Sattari Hub.app"

echo "== cleaning stage =="
rm -rf "$stage_root"
mkdir -p "$payload_root"

# ---------------------------------------------------------------------------
# 1) Build the Hub Swift app and assemble its .app bundle.
# ---------------------------------------------------------------------------
echo "== building Sattari Hub (release) =="
swift build --package-path "$repo_root/apps/sattari-hub" -c release

hub_bin="$repo_root/apps/sattari-hub/.build/release/SattariHub"
[ -x "$hub_bin" ] || { echo "Hub binary not produced at $hub_bin"; exit 2; }

echo "== assembling Sattari Hub.app =="
mkdir -p "$hub_app/Contents/MacOS"
mkdir -p "$hub_app/Contents/Resources"
mkdir -p "$hub_app/Contents/Resources/Payload"

cp "$hub_bin" "$hub_app/Contents/MacOS/SattariHub"
cp "$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json" \
   "$hub_app/Contents/Resources/sattari-products.json"

cat > "$hub_app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>            <string>Sattari Hub</string>
  <key>CFBundleDisplayName</key>     <string>Sattari Hub</string>
  <key>CFBundleIdentifier</key>      <string>com.Sattari.SattariHub</string>
  <key>CFBundleVersion</key>         <string>0.1.0</string>
  <key>CFBundleShortVersionString</key> <string>0.1.0</string>
  <key>CFBundleExecutable</key>      <string>SattariHub</string>
  <key>CFBundlePackageType</key>     <string>APPL</string>
  <key>LSMinimumSystemVersion</key>  <string>13.0</string>
  <key>NSHighResolutionCapable</key> <true/>
  <key>NSPrincipalClass</key>        <string>NSApplication</string>
</dict>
</plist>
EOF

cat > "$hub_app/Contents/PkgInfo" <<<"APPLSatr"

# ---------------------------------------------------------------------------
# 2) Copy every required alpha payload into the Hub's bundled Payload/.
#    The Hub reads the manifest and finds these via Bundle.main.resourceURL.
# ---------------------------------------------------------------------------
echo "== bundling plugin payloads =="
# The manifest paths are rooted at build/all (the canonical output tree). Set
# SATTARI_BUILD_TREE=build/local to SOURCE payloads from a different build tree while
# keeping the Hub's internal layout (and thus runtime lookup) identical.
build_tree="${SATTARI_BUILD_TREE:-build/all}"
echo "   sourcing payloads from: $build_tree"
copied=0; missing=0
validate_bundle() {
    local bundle="$1"
    [ -d "$bundle" ] || { echo "ERROR: bundle directory missing: $bundle" >&2; return 1; }
    [ -f "$bundle/Contents/Info.plist" ] || { echo "ERROR: bundle Info.plist missing: $bundle" >&2; return 1; }
    local executable
    executable="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f -perm -111 -print -quit 2>/dev/null)"
    [ -n "$executable" ] || { echo "ERROR: bundle executable missing: $bundle" >&2; return 1; }
}
while IFS= read -r relative_path; do
    src_rel="${relative_path/build\/all/$build_tree}"
    src="$repo_root/$src_rel"
    validate_bundle "$src"
    dst="$hub_app/Contents/Resources/Payload/$relative_path"
    mkdir -p "$(dirname "$dst")"
    # Use ditto so .app/.vst3/.component bundle attrs survive intact.
    ditto "$src" "$dst"
    validate_bundle "$dst"
    copied=$((copied + 1))
done < <(python3 -c "
import json,sys
with open('$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json') as f:
    d=json.load(f)
for p in d['products']:
    if p.get('shippingProfile') != 'alpha':
        continue
    for b in p['bundles']:
        print(b['relativeSourcePath'])
")
[ "$copied" -eq 9 ] || { echo "ERROR: expected 9 complete alpha bundles, copied $copied" >&2; exit 2; }
echo "   copied and validated $copied alpha bundle(s)"

echo "== bundling GPL notices and source offer =="
legal="$hub_app/Contents/Resources/Legal"
mkdir -p "$legal"
cp "$repo_root/LICENSE" "$legal/GPL-3.0.txt"
cp "$repo_root/LICENSING.md" "$legal/LICENSING.md"
cp "$repo_root/third_party/JUCE/LICENSE.md" "$legal/JUCE-LICENSE.md"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
source_remote="$(git -C "$repo_root" remote get-url origin)"
cat > "$legal/SOURCE-OFFER.txt" <<EOF
Sattari Audio complete corresponding source

Build commit: $source_commit
Source repository: $source_remote

The repository at that commit, including pinned submodules and build scripts, is
the complete corresponding source for this GPLv3 binary distribution.
EOF

# ---------------------------------------------------------------------------
# 3) Ad-hoc sign the Hub.app (so it launches; replace with Developer ID later).
# ---------------------------------------------------------------------------
echo "== ad-hoc signing Hub.app =="
codesign --force --deep -s - "$hub_app" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 4) Build the .pkg with pkgbuild.
# ---------------------------------------------------------------------------
pkg_out="$out_dir/Sattari-Audio-Suite.pkg"
echo "== building $pkg_out =="
pkgbuild \
    --identifier com.Sattari.AudioSuite \
    --version 0.1.0 \
    --install-location / \
    --root "$stage_root" \
    "$pkg_out"

# Cleanup stage (keep the .pkg).
rm -rf "$stage_root"

echo
echo "== DONE =="
echo "Installer: $pkg_out"
echo "Hub.app contains $copied plugin payload(s)."
echo
echo "Sign + notarize for distribution:"
echo "  productsign --sign 'Developer ID Installer: ...' \\"
echo "    '$pkg_out' '${pkg_out%.pkg}-signed.pkg'"
echo "  xcrun notarytool submit '${pkg_out%.pkg}-signed.pkg' --keychain-profile <name> --wait"
