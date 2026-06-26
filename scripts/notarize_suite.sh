#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio — Developer ID sign + notarize + staple the suite installer
# -----------------------------------------------------------------------------
#  Turns the ad-hoc-signed local .pkg into a Gatekeeper-clean, distributable
#  installer. This script NEVER sees your Apple password: it uses a notarytool
#  *keychain profile* that you create once, separately:
#
#     xcrun notarytool store-credentials sattari-notary \
#         --apple-id "you@example.com" --team-id "TEAMID1234"
#     # (paste an app-specific password when prompted — stored in your keychain)
#
#  Then export the three identities and run this:
#     export SATTARI_DEV_APP_ID="Developer ID Application: Your Name (TEAMID1234)"
#     export SATTARI_DEV_INSTALLER_ID="Developer ID Installer: Your Name (TEAMID1234)"
#     export SATTARI_NOTARY_PROFILE="sattari-notary"
#     scripts/notarize_suite.sh
#
#  Order matters: every code bundle is deep-signed with the Application identity
#  with a secure timestamp + hardened runtime BEFORE the .pkg is rebuilt, then
#  the .pkg is productsigned with the Installer identity, submitted, and stapled.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

: "${SATTARI_DEV_APP_ID:?set SATTARI_DEV_APP_ID to your 'Developer ID Application: ...' identity}"
: "${SATTARI_DEV_INSTALLER_ID:?set SATTARI_DEV_INSTALLER_ID to your 'Developer ID Installer: ...' identity}"
: "${SATTARI_NOTARY_PROFILE:?set SATTARI_NOTARY_PROFILE to your notarytool keychain profile name}"

ARTROOT="${SATTARI_ARTEFACT_ROOT:-build/all/plugins}"
PKG_IN="${SATTARI_PKG:-build/installer/Sattari-Audio-Suite.pkg}"
PKG_SIGNED="${PKG_IN%.pkg}-signed.pkg"
ENTITLEMENTS="${SATTARI_ENTITLEMENTS:-}"

echo "== 1) Developer ID sign every code bundle (hardened runtime + timestamp) =="
sign_one() {
  local bundle="$1"
  local args=(--force --options runtime --timestamp --sign "$SATTARI_DEV_APP_ID")
  [ -n "$ENTITLEMENTS" ] && args+=(--entitlements "$ENTITLEMENTS")
  # Sign nested code first (deep), then the bundle itself.
  codesign "${args[@]}" --deep "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"
  echo "   signed: $bundle"
}

count=0
while IFS= read -r bundle; do
  sign_one "$bundle"; count=$((count+1))
done < <(find "$ARTROOT" -maxdepth 5 \( -name '*.component' -o -name '*.vst3' -o -name '*.app' \) 2>/dev/null)
echo "   $count code bundle(s) signed"
[ "$count" -gt 0 ] || { echo "ERROR: no code bundles under $ARTROOT (build the suite first)"; exit 2; }

echo "== 2) Rebuild the installer so it carries the Developer-ID-signed payloads =="
scripts/package_suite_installer.sh

echo "== 3) productsign the .pkg with the Installer identity =="
[ -f "$PKG_IN" ] || { echo "ERROR: installer not found at $PKG_IN"; exit 2; }
productsign --sign "$SATTARI_DEV_INSTALLER_ID" "$PKG_IN" "$PKG_SIGNED"
pkgutil --check-signature "$PKG_SIGNED"

echo "== 4) Submit to Apple notary service (waits for the verdict) =="
xcrun notarytool submit "$PKG_SIGNED" --keychain-profile "$SATTARI_NOTARY_PROFILE" --wait

echo "== 5) Staple the notarization ticket =="
xcrun stapler staple "$PKG_SIGNED"
xcrun stapler validate "$PKG_SIGNED"
spctl --assess --type install --verbose=2 "$PKG_SIGNED" || true

echo "== DONE =="
echo "Distributable installer: $PKG_SIGNED"
