#!/usr/bin/env bash
# =============================================================================
#  Sattari Suite — release gate
# -----------------------------------------------------------------------------
#  Runs everything that can fail a release, in one pass, and reports precisely
#  what broke. Three phases:
#
#    1. CONTRACTS  — every built *Contract executable in build/<tree>.
#    2. auval      — every installed .component. The AU TYPE IS READ FROM THE
#                    BUNDLE'S Info.plist, never guessed: this repo has aufx, aumf
#                    and aumu plugins and guessing has produced false failures
#                    (Gate is Gt01 not Gat1; Vocoder is aumf not aufx).
#    3. STATIC     — brand-registry/Hub-manifest sync + rack preset references.
#
#  auval is run ONE AT A TIME. Running it in a tight loop returns spurious
#  failures, which look exactly like real ones.
#
#  Usage:  scripts/release_check.sh [build-tree]      (default: build/local)
#  Exit 0 only if every phase passes.
# =============================================================================

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TREE="${1:-build/local}"
BUILD="$ROOT/$TREE"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"

fails=0
pass=0

hr() { printf '%s\n' "-------------------------------------------------------------"; }

# --- 1. Contracts ------------------------------------------------------------
hr; echo "CONTRACTS"; hr
while IFS= read -r exe; do
    name="$(basename "$exe")"
    out="$("$exe" 2>&1)"
    rc=$?
    last="$(printf '%s' "$out" | tail -1)"
    if [ $rc -eq 0 ]; then
        printf '  ok    %-42s %s\n' "$name" "$last"; pass=$((pass+1))
    else
        printf '  FAIL  %-42s %s\n' "$name" "$last"; fails=$((fails+1))
        printf '%s\n' "$out" | sed 's/^/          /' | tail -12
    fi
done < <(find "$BUILD" -type f -perm -111 -name '*Contract' 2>/dev/null | sort)

# --- 2. auval ----------------------------------------------------------------
hr; echo "auval (type read from each Info.plist)"; hr
for comp in "$COMPONENTS"/*.component; do
    [ -d "$comp" ] || continue
    plist="$comp/Contents/Info.plist"
    [ -f "$plist" ] || continue
    manu="$(plutil -extract AudioComponents.0.manufacturer raw "$plist" 2>/dev/null)"
    [ "$manu" = "Satr" ] || continue                       # only ours
    type="$(plutil -extract AudioComponents.0.type raw "$plist" 2>/dev/null)"
    sub="$(plutil -extract AudioComponents.0.subtype raw "$plist" 2>/dev/null)"
    nm="$(basename "$comp" .component)"
    if auval -v "$type" "$sub" "$manu" 2>&1 | grep -q "VALIDATION SUCCEEDED"; then
        printf '  ok    %-28s %s %s\n' "$nm" "$type" "$sub"; pass=$((pass+1))
    else
        printf '  FAIL  %-28s %s %s\n' "$nm" "$type" "$sub"; fails=$((fails+1))
    fi
done

# --- 3. Static checks --------------------------------------------------------
hr; echo "STATIC CHECKS"; hr
for chk in check_brand_registry_sync.py check_rack_presets.py; do
    if python3 "$ROOT/scripts/$chk" >/dev/null 2>&1; then
        printf '  ok    %s\n' "$chk"; pass=$((pass+1))
    else
        printf '  FAIL  %s\n' "$chk"; fails=$((fails+1))
        python3 "$ROOT/scripts/$chk" 2>&1 | sed 's/^/          /' | tail -12
    fi
done

hr
if [ $fails -eq 0 ]; then
    echo "RELEASE CHECK PASSED — $pass checks green"
    exit 0
fi
echo "RELEASE CHECK FAILED — $fails failing, $pass green"
exit 1
