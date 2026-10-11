#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio Suite - unified macOS .pkg installer
# -----------------------------------------------------------------------------
#  Produces a single double-clickable installer that drops the Sattari Hub.app
#  into /Applications. The Hub bundles every available plugin payload inside
#  itself; once installed, the user opens the Hub and installs the manifest's
#  curated alpha set into their user plug-in folders.
#
#  Usage:
#     scripts/package_suite_installer.sh [output-dir]
#
#  Default output: <repo>/build/installer/Sattari-Audio-Suite.pkg
#
#  Prerequisites (all standard macOS tooling):
#   * Legacy alpha bundles in build/all/plugins/.../Release/
#   * Neural Auto Pitch bundles from scripts/build_auto_pitch_neural.sh
#   * Swift toolchain (for the Hub Swift app build)
#   * /usr/bin/pkgbuild  (system; always present)
#
#  The nested Hub app is ad-hoc signed; the .pkg is intentionally unsigned.
#  For distribution to testers without Gatekeeper warnings, Developer ID sign
#  and notarize the produced .pkg separately:
#     productsign --sign "Developer ID Installer: ..." in.pkg out.pkg
#     xcrun notarytool submit out.pkg --keychain-profile <...> --wait
# =============================================================================
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# A clean tracked diff does not imply a reproducible binary: CMake globs and
# SwiftPM also compile untracked sources. Check before any build/staging work,
# including internal packages whose patch cannot contain untracked files.
untracked_build_sources="$(git -C "$repo_root" ls-files --others --exclude-standard -- CMakeLists.txt cmake core plugins apps/sattari-hub assets config)"
if [[ -n "$untracked_build_sources" ]]; then
    echo "untracked binary-bearing source prevents a traceable installer:" >&2
    echo "$untracked_build_sources" >&2
    exit 2
fi
source_state="clean"
if ! git -C "$repo_root" diff --quiet --ignore-submodules=all \
   || ! git -C "$repo_root" diff --cached --quiet --ignore-submodules=all; then
    if [[ "${SATTARI_INTERNAL_DIRTY_PACKAGE:-0}" != "1" ]]; then
        echo "tracked source is dirty; commit the exact source before packaging binaries" >&2
        echo "for a traceable internal-only artifact, explicitly set SATTARI_INTERNAL_DIRTY_PACKAGE=1" >&2
        exit 2
    fi
    source_state="internal-uncommitted"
fi
out_dir="${1:-$repo_root/build/installer}"
mkdir -p "$out_dir"

stage_root="$(mktemp -d "$out_dir/.suite-stage.XXXXXX")"
payload_root="$stage_root/Applications"
hub_app="$payload_root/Sattari Hub.app"
cleanup_stage() { rm -rf "$stage_root"; }
trap cleanup_stage EXIT

echo "== preparing stage =="
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
cp "$repo_root/apps/sattari-hub/Packaging/Info.plist" "$hub_app/Contents/Info.plist"

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
autopitch_build_dir="${SATTARI_AUTOPITCH_BUILD_DIR:-$repo_root/build/auto-pitch-neural}"
echo "   sourcing payloads from: $build_tree"
echo "   Auto Pitch neural payloads: $autopitch_build_dir"
copied=0
resolve_source() {
    local relative_path="$1"
    if [[ "$relative_path" == build/all/plugins/auto-pitch/* ]]; then
        printf '%s/%s\n' "$autopitch_build_dir" "${relative_path#build/all/plugins/auto-pitch/}"
    else
        printf '%s/%s\n' "$repo_root" "${relative_path/build\/all/$build_tree}"
    fi
}
validate_bundle() {
    local bundle="$1"
    [ -d "$bundle" ] || { echo "ERROR: bundle directory missing: $bundle" >&2; return 1; }
    [ -f "$bundle/Contents/Info.plist" ] || { echo "ERROR: bundle Info.plist missing: $bundle" >&2; return 1; }
    local executable
    executable="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f -perm -111 -print -quit 2>/dev/null)"
    [ -n "$executable" ] || { echo "ERROR: bundle executable missing: $bundle" >&2; return 1; }
}
while IFS= read -r relative_path; do
    src="$(resolve_source "$relative_path")"
    validate_bundle "$src"
    python3 "$repo_root/scripts/check_hub_bundle_version.py" "$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json" "$relative_path" "$src"
    if [[ "$relative_path" == build/all/plugins/auto-pitch/* ]]; then
        "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$src"
    fi
    dst="$hub_app/Contents/Resources/Payload/$relative_path"
    mkdir -p "$(dirname "$dst")"
    # Use ditto so .app/.vst3/.component bundle attrs survive intact.
    ditto "$src" "$dst"
    validate_bundle "$dst"
    python3 "$repo_root/scripts/check_hub_bundle_version.py" "$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json" "$relative_path" "$dst"
    if [[ "$relative_path" == build/all/plugins/auto-pitch/* ]]; then
        "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$dst"
    fi
    copied=$((copied + 1))
done < <(python3 -c "
import json,sys,os
with open('$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json') as f:
    d=json.load(f)
seen=set()
for p in d['products']:
    if p.get('shippingProfile') != 'alpha' or (p.get('portfolioStatus', 'active') not in ('active', 'specialist') and os.environ.get('SATTARI_INCLUDE_LEGACY') != '1'):
        continue
    for b in p['bundles']:
        path=b['relativeSourcePath']
        if path not in seen:
            seen.add(path)
            print(path)
")
expected="$(python3 -c "
import json,os
with open('$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json') as f:
    d=json.load(f)
print(len({b['relativeSourcePath'] for p in d['products'] if p.get('shippingProfile') == 'alpha' and (p.get('portfolioStatus', 'active') in ('active', 'specialist') or os.environ.get('SATTARI_INCLUDE_LEGACY') == '1') for b in p['bundles']}))
")"
[ "$copied" -eq "$expected" ] || { echo "ERROR: expected $expected complete alpha bundles, copied $copied" >&2; exit 2; }
echo "   copied and validated $copied alpha bundle(s)"

echo "== bundling GPL notices and source offer =="
legal="$hub_app/Contents/Resources/Legal"
mkdir -p "$legal"
cp "$repo_root/LICENSE" "$legal/GPL-3.0.txt"
cp "$repo_root/LICENSING.md" "$legal/LICENSING.md"
cp "$repo_root/third_party/JUCE/LICENSE.md" "$legal/JUCE-LICENSE.md"
cp "$repo_root/third_party/rubberband/COPYING" "$legal/RUBBERBAND-COPYING.txt"
# This is the same pinned SDK used by bundle_auto_pitch_runtime_deps.sh.
cp "$repo_root/third_party/onnxruntime-osx-universal2-1.22.0/LICENSE" "$legal/ONNXRUNTIME-LICENSE.txt"
cp "$repo_root/third_party/onnxruntime-osx-universal2-1.22.0/ThirdPartyNotices.txt" "$legal/ONNXRUNTIME-THIRD-PARTY-NOTICES.txt"
cp "$repo_root/plugins/auto-pitch/models/CREPE-LICENSE.txt" "$legal/CREPE-MIT-LICENSE.txt"
cp "$repo_root/plugins/auto-pitch/models/model-release.json" "$legal/CREPE-MODEL-PROVENANCE.json"
source_commit="$(git -C "$repo_root" rev-parse HEAD)"
source_remote="$(git -C "$repo_root" remote get-url origin)"
cat > "$legal/SOURCE-OFFER.txt" <<EOF
Sattari Audio complete corresponding source

Base commit: $source_commit
Source repository: $source_remote
Source state: $source_state

For a clean package, the repository at that commit is the complete corresponding
source. For an internal-uncommitted package, apply WORKTREE.patch to the base
commit and use the package-time helpers in Source-Tools/.
EOF
if [[ "$source_state" == "internal-uncommitted" ]]; then
    git -C "$repo_root" diff --binary HEAD > "$legal/WORKTREE.patch"
    git -C "$repo_root" status --short > "$legal/WORKTREE-STATUS.txt"
    mkdir -p "$legal/Source-Tools"
    cp "$repo_root/scripts/build_auto_pitch_neural.sh" "$legal/Source-Tools/"
    cp "$repo_root/scripts/bundle_auto_pitch_runtime_deps.sh" "$legal/Source-Tools/"
    cp "$repo_root/scripts/fetch_onnxruntime_macos.sh" "$legal/Source-Tools/"
    cp "$repo_root/scripts/fetch_crepe_onnx.py" "$legal/Source-Tools/"
    cp "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$legal/Source-Tools/"
    cp "$repo_root/scripts/verify_auto_pitch_models.py" "$legal/Source-Tools/"
fi

# ---------------------------------------------------------------------------
# 3) Ad-hoc sign the Hub.app (so it launches; replace with Developer ID later).
# ---------------------------------------------------------------------------
echo "== ad-hoc signing Hub.app =="
codesign --force --deep -s - "$hub_app" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 4) Build the .pkg with pkgbuild.
# ---------------------------------------------------------------------------
if [[ "$source_state" == "clean" ]]; then
    pkg_out="$out_dir/Sattari-Audio-Suite.pkg"
else
    pkg_out="$out_dir/Sattari-Audio-Suite-INTERNAL-UNCOMMITTED.pkg"
fi
echo "== building $pkg_out =="
pkgbuild \
    --identifier com.Sattari.AudioSuite \
    --version "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$hub_app/Contents/Info.plist")" \
    --install-location / \
    --root "$stage_root" \
    "$pkg_out"

echo
echo "== DONE =="
echo "Installer: $pkg_out"
echo "Hub.app contains $copied plugin payload(s)."
echo
echo "Sign + notarize for distribution:"
echo "  productsign --sign 'Developer ID Installer: ...' \\"
echo "    '$pkg_out' '${pkg_out%.pkg}-signed.pkg'"
echo "  xcrun notarytool submit '${pkg_out%.pkg}-signed.pkg' --keychain-profile <name> --wait"
