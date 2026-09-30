#!/usr/bin/env bash
# =============================================================================
#  Install the PACKAGED release candidate (Auto Pitch + Sattari Key) on this Mac for DAW
#  qualification, and prove what is installed.
#  The artefact is dist/candidate-<sha>/ from scripts/package_candidate.sh: self-contained
#  bundles (runtime embedded, no build-machine rpaths) - what a user gets, not the raw dev build.
#  Refuses while a DAW is running (replacing a loaded plugin bundle is unsafe). Replaced bundles
#  are moved to /tmp, never deleted. Prints the sha256 of every installed binary for the DAW
#  checklist header.
#
#  Usage: scripts/install_candidate.sh <candidate sha> [--check-only]
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
C="$(git rev-parse --short "${1:?usage: install_candidate.sh <candidate sha> [--check-only]}")" || exit 2
CHECK_ONLY="${2:-}"
PKG="dist/candidate-$C"
[ -d "$PKG" ] || { echo "ABORT: $PKG missing - run scripts/package_candidate.sh $C first"; exit 2; }
changed="$(git diff --name-only "$C" HEAD -- plugins core cmake CMakeLists.txt third_party | grep -v -E '^plugins/[^/]+/(tests|tools)/' || true)"
[ -z "$changed" ] || { echo "ABORT: plugin sources differ from $C:"; echo "$changed"; exit 2; }
PI="$HOME/Library/Audio/Plug-Ins"
sha() { shasum -a 256 "$1" 2>/dev/null | cut -c1-16; }
# product dir | bundle | install dir | executable
ITEMS=("Auto Pitch|Sattari Auto Pitch.component|Components|Sattari Auto Pitch"
       "Auto Pitch|Sattari Auto Pitch.vst3|VST3|Sattari Auto Pitch"
       "Sattari Key|Sattari Key.component|Components|Sattari Key"
       "Sattari Key|Sattari Key.vst3|VST3|Sattari Key")
same() {  # whole bundle identical (binary, embedded runtime, plist, resources)
  diff -rq "$PKG/$1/$2" "$PI/$3/$2" >/dev/null 2>&1
}
fail=0; todo=()
for it in "${ITEMS[@]}"; do
  IFS='|' read -r prod bundle dir exe <<<"$it"
  if same "$prod" "$bundle" "$dir"; then echo "OK    $bundle == packaged candidate ($(sha "$PI/$dir/$bundle/Contents/MacOS/$exe"))"
  else echo "DIFF  $bundle installed is not the packaged candidate"; todo+=("$it"); fi
done
if [ ${#todo[@]} -gt 0 ]; then
  if [ "$CHECK_ONLY" = "--check-only" ]; then fail=1
  else
    if pgrep -qf "Logic Pro|Ableton Live|FL Studio|MainStage|GarageBand"; then
      echo "ABORT: quit every DAW first (running: $(pgrep -lf 'Logic Pro|Ableton Live|FL Studio|MainStage|GarageBand' | awk '{print $2}' | xargs))"; exit 2
    fi
    stamp="$(date +%s)"
    for it in "${todo[@]}"; do
      IFS='|' read -r prod bundle dir exe <<<"$it"
      [ -e "$PI/$dir/$bundle" ] && mv "$PI/$dir/$bundle" "/tmp/$bundle.replaced-$stamp"
      ditto "$PKG/$prod/$bundle" "$PI/$dir/$bundle"
      same "$prod" "$bundle" "$dir" && echo "OK    installed $bundle" || { echo "FAIL  $bundle did not install identically"; fail=1; }
    done
    killall -9 AudioComponentRegistrar 2>/dev/null || true    # refresh the AU cache
    for au in "aufx Ap01 Satr" "aumf Sk01 Satr"; do
      auval -v $au 2>&1 | grep -qE "AU VALIDATION SUCCEEDED" && echo "OK    auval $au" || { echo "FAIL  auval $au"; fail=1; }
    done
  fi
fi
echo; echo "Paste into the DAW checklist header (candidate $C, packaged):"
for it in "${ITEMS[@]}"; do IFS='|' read -r prod bundle dir exe <<<"$it"; echo "  $(sha "$PI/$dir/$bundle/Contents/MacOS/$exe")  $dir/$bundle"; done
exit $fail
