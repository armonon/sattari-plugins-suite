#!/usr/bin/env bash
# Build, contract-test, Developer-ID sign, package, notarize, and staple StemDeck.
# This script intentionally refuses to fall back to ad-hoc signing: an artifact
# produced here is either a verified distribution build or no artifact at all.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${STEMDECK_RELEASE_BUILD_DIR:-$repo_root/build/stemdeck-release}"
output_dir="${1:-$repo_root/build/releases}"
version="${STEMDECK_RELEASE_VERSION:-0.5.0}"
entitlements="${SATTARI_ENTITLEMENTS:-$repo_root/packaging/sattari-plugin.entitlements}"

: "${SATTARI_DEV_APP_ID:?set SATTARI_DEV_APP_ID to a Developer ID Application identity}"
: "${SATTARI_DEV_INSTALLER_ID:?set SATTARI_DEV_INSTALLER_ID to a Developer ID Installer identity}"
: "${SATTARI_NOTARY_PROFILE:?set SATTARI_NOTARY_PROFILE to a notarytool keychain profile}"

for tool in cmake ninja codesign pkgbuild productbuild productsign xcrun; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "missing required tool: $tool" >&2
    exit 2
  }
done
[[ -f "$entitlements" ]] || {
  echo "missing hardened-runtime entitlements: $entitlements" >&2
  exit 2
}

cd "$repo_root"
mkdir -p "$output_dir"

# Neural features are ON for a release build. They were OFF, which meant every
# shipped binary had the beat model, the key model and on-device separation
# compiled out — the confidence gating, the anti-alias pre-filter and the
# shutdown cancellation were all dead code in the product.
#
# TRADE-OFF, stated because it is user-visible: an ONNX-enabled build adopts the
# runtime's macOS floor (13.3 for onnxruntime 1.22.0) instead of the suite's
# 10.13. No shippable onnxruntime targets anything older — arm64 macOS itself
# starts at 11.0. Ship without neural features by setting SATTARI_USE_ONNX=OFF.
use_onnx="${SATTARI_USE_ONNX:-ON}"
if [[ "$use_onnx" == "ON" ]]; then
  if [[ -z "$(echo "$repo_root"/third_party/onnxruntime-osx-universal2-*/lib/libonnxruntime.dylib 2>/dev/null | grep -v '\*')" ]]; then
    echo "SATTARI_USE_ONNX=ON but no vendored ONNX Runtime found." >&2
    echo "Run scripts/fetch_onnxruntime.sh first, or set SATTARI_USE_ONNX=OFF." >&2
    exit 2
  fi
  echo "NOTE: neural features ON -> this release will NOT load below the ONNX"
  echo "      Runtime's macOS floor. Set SATTARI_USE_ONNX=OFF to ship without them."
fi

cmake_args=(
  -S plugins/stemdeck -B "$build_dir" -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
  "-DSATTARI_USE_ONNX=${use_onnx}"
)
if command -v ccache >/dev/null 2>&1; then
  cmake_args+=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
fi
cmake "${cmake_args[@]}"
cmake --build "$build_dir" --target \
  SattariStemDeck_Standalone SattariStemDeck_VST3 StemDeckContractSuite \
  -j "${JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure -L contract

release_dir="$build_dir/SattariStemDeck_artefacts/Release"
app="$release_dir/Standalone/Sattari StemDeck.app"
vst3="$release_dir/VST3/Sattari StemDeck.vst3"
[[ -d "$app" && -d "$vst3" ]] || {
  echo "StemDeck app or VST3 artifact is missing from $release_dir" >&2
  exit 2
}

sign_bundle() {
  local bundle="$1"
  while IFS= read -r nested; do
    codesign --force --options runtime --timestamp \
      --entitlements "$entitlements" --sign "$SATTARI_DEV_APP_ID" "$nested"
  done < <(find "$bundle/Contents" -type f \
    -name '*.dylib' 2>/dev/null)
  while IFS= read -r framework; do
    codesign --force --options runtime --timestamp \
      --entitlements "$entitlements" --sign "$SATTARI_DEV_APP_ID" "$framework"
  done < <(find "$bundle/Contents" -type d -name '*.framework' 2>/dev/null)
  codesign --force --options runtime --timestamp \
    --entitlements "$entitlements" --sign "$SATTARI_DEV_APP_ID" "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"
}

/usr/bin/xattr -cr "$app" "$vst3" || true

# Embed libonnxruntime and rewrite @rpath BEFORE Developer ID signing.
#
# This script builds with SATTARI_USE_ONNX=ON by default (see above), the vendored dylib's
# install name is @rpath/libonnxruntime.<ver>.dylib, and NOTHING in CMake copies it into a
# bundle. selfcontain_onnx.sh is the only thing that embeds the runtime and fixes the rpath
# — and until now only run_stemdeck_release_gate.sh and CI called it, never this script. The
# result was a signed, notarized, stapled installer whose only rpath pointed at the build
# machine's gitignored third_party/onnxruntime-*/lib, so the shipped app failed at dyld load
# on every machine except the one that built it.
#
# Order matters: selfcontain_onnx.sh ad-hoc signs what it rewrites, so it must run before
# sign_bundle or the ad-hoc signature would replace the Developer ID one.
"$repo_root/scripts/selfcontain_onnx.sh" "$app"
"$repo_root/scripts/selfcontain_onnx.sh" "$vst3"

sign_bundle "$app"
sign_bundle "$vst3"

# Validate the real shipping artifact, after signing, so the check sees exactly what the
# customer installs: no absolute /opt/homebrew links, @rpath dylibs actually present in
# Contents/Frameworks, and the @executable_path/../Frameworks rpath in place. The alpha
# packager has always done this; the release path never did.
EXPECTED_STEMDECK_APP_VERSION="$version" \
  "$repo_root/scripts/validate_stemdeck_app_bundle.sh" "$app"

stage="$(mktemp -d "${TMPDIR:-/tmp}/stemdeck-release.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
pkgbuild --component "$app" --install-location /Applications \
  --identifier com.Sattari.SattariStemDeck.app --version "$version" \
  "$stage/StemDeck-App.pkg"
pkgbuild --component "$vst3" --install-location /Library/Audio/Plug-Ins/VST3 \
  --identifier com.Sattari.SattariStemDeck.vst3 --version "$version" \
  "$stage/StemDeck-VST3.pkg"

cat > "$stage/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Sattari StemDeck $version</title>
  <organization>com.Sattari</organization>
  <domains enable_localSystem="true"/>
  <options customize="allow" require-scripts="false"/>
  <choices-outline>
    <line choice="app"><line choice="vst3"/></line>
  </choices-outline>
  <choice id="app" title="StemDeck Application" description="Install the standalone app">
    <pkg-ref id="com.Sattari.SattariStemDeck.app"/>
  </choice>
  <choice id="vst3" title="StemDeck VST3" description="Install the VST3 plug-in">
    <pkg-ref id="com.Sattari.SattariStemDeck.vst3"/>
  </choice>
  <pkg-ref id="com.Sattari.SattariStemDeck.app" version="$version">StemDeck-App.pkg</pkg-ref>
  <pkg-ref id="com.Sattari.SattariStemDeck.vst3" version="$version">StemDeck-VST3.pkg</pkg-ref>
</installer-gui-script>
EOF

unsigned_pkg="$stage/Sattari-StemDeck-$version-unsigned.pkg"
final_pkg="$output_dir/Sattari-StemDeck-$version.pkg"
productbuild --distribution "$stage/distribution.xml" \
  --package-path "$stage" "$unsigned_pkg"
productsign --sign "$SATTARI_DEV_INSTALLER_ID" "$unsigned_pkg" "$final_pkg"
pkgutil --check-signature "$final_pkg"

xcrun notarytool submit "$final_pkg" \
  --keychain-profile "$SATTARI_NOTARY_PROFILE" --wait
xcrun stapler staple "$final_pkg"
xcrun stapler validate "$final_pkg"
spctl --assess --type install --verbose=2 "$final_pkg"

shasum -a 256 "$final_pkg" > "$final_pkg.sha256"
echo "Release ready: $final_pkg"
