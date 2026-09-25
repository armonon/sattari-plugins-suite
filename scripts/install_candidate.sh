#!/usr/bin/env bash
# =============================================================================
#  Install the release-candidate plugins on THIS Mac for DAW qualification, and prove it.
#  - Auto Pitch: the fingerprinted build in build/auto-pitch-neural (already installed by its
#    build); verified against the candidate's qualification report, never rebuilt here.
#  - Sattari Key: build/song-key-rc (built from the same candidate sources with HOME redirected,
#    so the build itself installed nothing); copied into ~/Library/Audio/Plug-Ins here.
#  Refuses while a DAW is running (replacing a loaded plugin bundle is unsafe) and prints the
#  sha256 of every installed binary for the DAW checklist header.
#
#  Usage: scripts/install_candidate.sh <candidate sha> [--check-only]
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
C="$(git rev-parse --short "${1:?usage: install_candidate.sh <candidate sha> [--check-only]}")" || exit 2
CHECK_ONLY="${2:-}"
REPORT="$(ls validation/auto-pitch/qualification/*-"$C".md 2>/dev/null | head -n1)"
[ -n "$REPORT" ] || { echo "ABORT: no qualification report for $C"; exit 2; }
changed="$(git diff --name-only "$C" HEAD -- plugins core cmake CMakeLists.txt third_party | grep -v -E '^plugins/[^/]+/(tests|tools)/' || true)"
[ -z "$changed" ] || { echo "ABORT: plugin sources differ from $C:"; echo "$changed"; exit 2; }
PI="$HOME/Library/Audio/Plug-Ins"
sha() { shasum -a 256 "$1" 2>/dev/null | cut -c1-16; }
fail=0
ap_vst3="$PI/VST3/Sattari Auto Pitch.vst3/Contents/MacOS/Sattari Auto Pitch"
want="$(grep -E '^\| installed VST3 \| `' "$REPORT" | head -n1 | sed -E 's/.*`([0-9a-f]+)`.*/\1/')"
if [ "$(sha "$ap_vst3")" = "$want" ]; then echo "OK    Auto Pitch VST3 $(sha "$ap_vst3") == candidate"
else echo "FAIL  Auto Pitch VST3 $(sha "$ap_vst3") != candidate $want"; fail=1; fi
built_au="$(find build/auto-pitch-neural -path '*Release/AU/Sattari Auto Pitch.component/Contents/MacOS/Sattari Auto Pitch' | head -n1)"
ap_au="$PI/Components/Sattari Auto Pitch.component/Contents/MacOS/Sattari Auto Pitch"
if cmp -s "$built_au" "$ap_au"; then echo "OK    Auto Pitch AU   $(sha "$ap_au") == candidate build"; else echo "FAIL  Auto Pitch AU differs from candidate build"; fail=1; fi

SK="build/song-key-rc/SattariSongKey_artefacts/Release"
[ -d "$SK/AU/Sattari Key.component" ] || { echo "ABORT: $SK missing (build song-key-rc from $C first)"; exit 2; }
sk_ok=1
for fmt in "Components|AU/Sattari Key.component" "VST3|VST3/Sattari Key.vst3"; do
  dir="${fmt%%|*}"; rel="${fmt#*|}"; name="$(basename "$rel")"
  cmp -s "$SK/$rel/Contents/MacOS/Sattari Key" "$PI/$dir/$name/Contents/MacOS/Sattari Key" || sk_ok=0
done
if [ $sk_ok -eq 1 ]; then echo "OK    Sattari Key AU+VST3 == candidate build ($(sha "$SK/AU/Sattari Key.component/Contents/MacOS/Sattari Key") / $(sha "$SK/VST3/Sattari Key.vst3/Contents/MacOS/Sattari Key"))"
elif [ "$CHECK_ONLY" = "--check-only" ]; then echo "FAIL  Sattari Key installed is NOT the candidate build"; fail=1
else
  if pgrep -qf "Logic Pro|Ableton Live|FL Studio|MainStage|GarageBand"; then
    echo "ABORT: quit every DAW first (running: $(pgrep -lf 'Logic Pro|Ableton Live|FL Studio|MainStage|GarageBand' | awk '{print $2}' | xargs))"; exit 2
  fi
  for fmt in "Components|AU/Sattari Key.component" "VST3|VST3/Sattari Key.vst3"; do
    dir="${fmt%%|*}"; rel="${fmt#*|}"; name="$(basename "$rel")"
    [ -e "$PI/$dir/$name" ] && mv "$PI/$dir/$name" "/tmp/$name.replaced-$(date +%s)"   # kept, not deleted
    ditto "$SK/$rel" "$PI/$dir/$name"
  done
  killall -9 AudioComponentRegistrar 2>/dev/null || true    # AU cache refresh
  echo "OK    Sattari Key installed from candidate build ($(sha "$PI/Components/Sattari Key.component/Contents/MacOS/Sattari Key"))"
  auval -v aumf Sk01 Satr 2>&1 | grep -E "AU VALIDATION (SUCCEEDED|FAILED)"
fi
echo; echo "Paste into the DAW checklist header:"
for f in "$ap_au" "$ap_vst3" "$PI/Components/Sattari Key.component/Contents/MacOS/Sattari Key" "$PI/VST3/Sattari Key.vst3/Contents/MacOS/Sattari Key"; do
  echo "  $(sha "$f")  ${f#$PI/}"; done
exit $fail
