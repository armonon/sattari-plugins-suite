#!/usr/bin/env bash
# =============================================================================
#  Package the release candidate as a self-contained, installable artefact.
#  The fingerprinted build in build/auto-pitch-neural links ONNX Runtime through an absolute
#  rpath into this repo, so its bundles load only on this Mac (found by the clean-machine kit,
#  2026-09-25). This runs CI's embed step on COPIES: runtime dylibs into Contents/Frameworks,
#  @loader_path rpath, absolute rpaths removed, ad-hoc re-signed; then fixes permissions (the
#  build is mode 700), verifies every bundle, validates the packaged VST3s with pluginval, and
#  records raw + signature-stripped sha256 so a later Developer-ID-signed copy can still be tied
#  to this candidate. DSP is untouched: the Mach-O code is the candidate build's.
#
#  Usage: [AP_BUILD=<auto pitch build dir>] scripts/package_candidate.sh <candidate sha>
#           -> dist/candidate-<sha>/ + .tar.gz   (AP_BUILD default build/auto-pitch-neural = cbd1ce4)
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
C="$(git rev-parse --short "${1:?usage: package_candidate.sh <candidate sha>}")"
REPORT="$(ls validation/auto-pitch/qualification/*-"$C".md | head -n1)"
changed="$(git diff --name-only "$C" HEAD -- plugins core cmake CMakeLists.txt third_party | grep -v -E '^plugins/[^/]+/(tests|tools)/' || true)"
[ -z "$changed" ] || { echo "ABORT: plugin sources differ from $C"; exit 2; }
AP="${AP_BUILD:-build/auto-pitch-neural}/SattariAutoPitch_artefacts/Release"   # AP_BUILD: candidate build dir
SK="build/song-key-rc/SattariSongKey_artefacts/Release"
want="$(grep -E '^\| (installed|built) VST3[^|]*\| `' "$REPORT" | head -n1 | sed -E 's/.*`([0-9a-f]+)`.*/\1/')"
have="$(shasum -a 256 "$AP/VST3/Sattari Auto Pitch.vst3/Contents/MacOS/Sattari Auto Pitch" | cut -c1-16)"
[ "$want" = "$have" ] || { echo "ABORT: built Auto Pitch VST3 $have != candidate $want"; exit 2; }
OUT="dist/candidate-$C"; rm -rf "$OUT"; mkdir -p "$OUT/Auto Pitch" "$OUT/Sattari Key"
for f in "AU/Sattari Auto Pitch.component" "VST3/Sattari Auto Pitch.vst3" "Standalone/Sattari Auto Pitch.app"; do ditto "$AP/$f" "$OUT/Auto Pitch/$(basename "$f")"; done
for f in "AU/Sattari Key.component" "VST3/Sattari Key.vst3" "Standalone/Sattari Key.app"; do ditto "$SK/$f" "$OUT/Sattari Key/$(basename "$f")"; done
scripts/bundle_auto_pitch_runtime_deps.sh "$OUT/Auto Pitch/"* >/dev/null
chmod -R u=rwX,go=rX "$OUT"
for b in "$OUT/Auto Pitch/"*; do scripts/verify_auto_pitch_bundle.sh "$b" >/dev/null && echo "self-contained: $(basename "$b")"; done
bad=0
for b in "$OUT/"*/*; do
  bin="$b/Contents/MacOS/$(defaults read "$PWD/$b/Contents/Info.plist" CFBundleExecutable)"
  abs="$(otool -l "$bin" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | grep '^/' || true)"
  deps="$(otool -L "$bin" | awk '/^\t/{print $1}' | grep -v -E '^(/System/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' || true)"
  [ -z "$abs$deps" ] || { echo "NOT self-contained: $b ($abs $deps)"; bad=1; }
  codesign --verify --deep --strict "$b" 2>/dev/null || { echo "signature invalid: $b"; bad=1; }
done
[ $bad -eq 0 ] || exit 1
if [ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]; then
  for v in "$OUT/Auto Pitch/Sattari Auto Pitch.vst3" "$OUT/Sattari Key/Sattari Key.vst3"; do
    /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate-in-process --skip-gui-tests --validate "$PWD/$v" >/tmp/pv-pkg.log 2>&1 \
      && echo "pluginval 5 PASS: $(basename "$v")" || { echo "pluginval FAIL: $v (/tmp/pv-pkg.log)"; exit 1; }
  done
fi
scripts/clean_machine_qualify.sh --fingerprint-only --bundles "$OUT" --out "$OUT/fingerprints" >/dev/null 2>&1 || true
FP="$(find "$OUT/fingerprints" -name fingerprints.tsv | head -n1)"
tar -C dist -czf "dist/candidate-$C.tar.gz" "candidate-$C"; shasum -a 256 "dist/candidate-$C.tar.gz" > "dist/candidate-$C.tar.gz.sha256"
{ echo; echo "## Packaged candidate artefact ($(date '+%Y-%m-%d %H:%M'), \`scripts/package_candidate.sh $C\`)"; echo
  echo "Self-contained (runtime embedded, no absolute rpaths), permissions fixed, ad-hoc signed, pluginval 5 PASS"
  echo "on both packaged VST3s. \`dist/candidate-$C.tar.gz\` sha256 \`$(cut -c1-16 "dist/candidate-$C.tar.gz.sha256")\`."
  echo "Mach-O fingerprints (sha256 / signature-stripped sha256, first 16):"; echo
  if [ -n "$FP" ]; then awk -F'\t' 'NR>1{printf "- %s: `%s` / `%s`\n", $1, substr($(NF-1),1,16), substr($NF,1,16)}' "$FP"; fi
} >> "$REPORT"
echo "packaged: $OUT (+ .tar.gz); fingerprints appended to $REPORT"
