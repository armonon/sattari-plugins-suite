#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio Suite - unified macOS .pkg installer
# -----------------------------------------------------------------------------
#  Produces a single double-clickable installer that drops the Sattari Hub.app
#  into /Applications. The Hub bundles every available plugin payload inside
#  itself; once installed, the user opens the Hub and clicks "Install All" to
#  copy AU + VST3 + Standalone bundles into their system plug-in folders.
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
# 2) Copy every available plugin payload into the Hub's bundled Payload/.
#    The Hub reads the manifest and finds these via Bundle.main.resourceURL.
# ---------------------------------------------------------------------------
echo "== bundling plugin payloads =="
# The manifest paths are rooted at build/all (the canonical output tree). Set
# SATTARI_BUILD_TREE=build/local to SOURCE payloads from a different build tree while
# keeping the Hub's internal layout (and thus runtime lookup) identical.
build_tree="${SATTARI_BUILD_TREE:-build/all}"
echo "   sourcing payloads from: $build_tree"
copied=0; missing=0
while IFS= read -r relative_path; do
    src_rel="${relative_path/build\/all/$build_tree}"
    src="$repo_root/$src_rel"
    if [ -e "$src" ]; then
        dst="$hub_app/Contents/Resources/Payload/$relative_path"
        mkdir -p "$(dirname "$dst")"
        # Use ditto so .app/.vst3/.component bundle attrs survive intact.
        ditto "$src" "$dst"
        copied=$((copied + 1))
    else
        echo "  WARN: missing $src_rel"
        missing=$((missing + 1))
    fi
done < <(python3 -c "
import json,sys
with open('$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json') as f:
    d=json.load(f)
for p in d['products']:
    for b in p['bundles']:
        print(b['relativeSourcePath'])
")
echo "   copied $copied bundle(s); $missing missing (those plugins must be built first)"

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
