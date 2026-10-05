#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio — signing / notarization PREFLIGHT ("doctor")
# -----------------------------------------------------------------------------
#  Read-only. Checks that everything notarize_suite.sh needs is in place and, for
#  anything missing, prints the exact command or Apple step to fix it. Run this
#  first — it never signs, submits, or changes anything.
#
#      scripts/sign_preflight.sh
# =============================================================================
set -uo pipefail

ok()   { printf "  \033[32m✓\033[0m %s\n" "$1"; }
bad()  { printf "  \033[31m✗\033[0m %s\n" "$1"; MISSING=$((MISSING+1)); }
info() { printf "      %s\n" "$1"; }
MISSING=0

echo "Sattari signing preflight"
echo "========================="

echo "1) Xcode command-line tools (codesign / notarytool / stapler)"
if xcrun --find notarytool >/dev/null 2>&1; then ok "notarytool present"; else
  bad "notarytool not found"; info "Install: xcode-select --install   (or install Xcode)"; fi

echo "2) Developer ID Application certificate (signs plugins/apps)"
APP_ID_LINE=$(security find-identity -v -p codesigning 2>/dev/null | grep "Developer ID Application" | head -1 || true)
if [ -n "$APP_ID_LINE" ]; then
  ok "found:$(echo "$APP_ID_LINE" | sed 's/^[[:space:]]*[0-9]*)//')"
  info "export SATTARI_DEV_APP_ID=\"$(echo "$APP_ID_LINE" | sed -E 's/.*\"(.*)\".*/\1/')\""
else
  bad "no 'Developer ID Application' certificate in the keychain"
  info "Get one at https://developer.apple.com/account (Certificates) — needs a paid Apple Developer account,"
  info "then download + double-click the .cer to install it into your login keychain."
fi

echo "3) Developer ID Installer certificate (signs the .pkg)"
INST_ID_LINE=$(security find-identity -v 2>/dev/null | grep "Developer ID Installer" | head -1 || true)
if [ -n "$INST_ID_LINE" ]; then
  ok "found:$(echo "$INST_ID_LINE" | sed 's/^[[:space:]]*[0-9]*)//')"
  info "export SATTARI_DEV_INSTALLER_ID=\"$(echo "$INST_ID_LINE" | sed -E 's/.*\"(.*)\".*/\1/')\""
else
  bad "no 'Developer ID Installer' certificate in the keychain"
  info "Same portal as above — create a 'Developer ID Installer' certificate and install it."
fi

echo "4) notarytool keychain profile (stores your app-specific password securely)"
PROFILE="${SATTARI_NOTARY_PROFILE:-sattari-notary}"
if xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1; then
  ok "keychain profile '$PROFILE' works"
else
  bad "keychain profile '$PROFILE' not set up (or invalid)"
  info "Create an app-specific password at https://appleid.apple.com (Sign-In & Security), then:"
  info "xcrun notarytool store-credentials $PROFILE --apple-id you@example.com --team-id TEAMID1234"
  info "(paste the app-specific password when prompted — it is stored in your keychain, never in the repo)"
fi

echo "5) Entitlements + build artefacts"
[ -f packaging/sattari-plugin.entitlements ] && ok "entitlements present" || bad "packaging/sattari-plugin.entitlements missing"
[ -f packaging/sattari-standalone.entitlements ] && ok "standalone audio-input entitlements present" || bad "packaging/sattari-standalone.entitlements missing"
[ -f packaging/sattari-modular-standalone.entitlements ] && ok "Modular hosting entitlements present" || bad "packaging/sattari-modular-standalone.entitlements missing"
ARTROOT="${SATTARI_ARTEFACT_ROOT:-}"
if [ -z "$ARTROOT" ]; then
  for cand in build/local/plugins build/all/plugins; do
    [ -d "$cand" ] && ARTROOT="$cand" && break
  done
fi
N=$(find "${ARTROOT:-/nonexistent}" -maxdepth 5 \( -name '*.component' -o -name '*.vst3' -o -name '*.app' \) 2>/dev/null | wc -l | tr -d ' ')
if [ "${N:-0}" -gt 0 ]; then ok "$N code bundle(s) found under ${ARTROOT}"; else
  bad "no built code bundles found"; info "Build first, e.g.: cmake --build build/local"; fi

echo
if [ "$MISSING" -eq 0 ]; then
  echo "All set. You can run: scripts/notarize_suite.sh"
else
  echo "$MISSING item(s) need attention before notarize_suite.sh will succeed."
  exit 1
fi
