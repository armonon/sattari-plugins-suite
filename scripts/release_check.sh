#!/usr/bin/env bash
# =============================================================================
#  Sattari Suite — release gate
# -----------------------------------------------------------------------------
#  Runs everything that can fail a release, in one pass, and reports precisely
#  what broke. Three phases:
#
#    0. BUILD      — every _AU and *Contract target, so the things validated below
#                    are the things in the working tree (skip with --no-build).
#    0b. FRESHNESS — hard-fails if an installed .component does not match what
#                    this tree built, or if two bundles claim one plug-in ID.
#    1. CONTRACTS  — the full registered CTest inventory, including smoke modes.
#    2. auval      — every installed .component except products marked "excluded"
#                    in config/products.json, which are SKIPPED OUT LOUD and
#                    counted separately. The AU TYPE IS READ FROM THE
#                    BUNDLE'S Info.plist, never guessed: this repo has aufx, aumf
#                    and aumu plugins and guessing has produced false failures
#                    (Gate is Gt01 not Gat1; Vocoder is aumf not aufx).
#    3. STATIC     — brand-registry/Hub-manifest sync + rack preset references.
#
#  auval is run ONE AT A TIME. Running it in a tight loop returns spurious
#  failures, which look exactly like real ones.
#
#  Usage:  scripts/release_check.sh [build-tree] [--no-build]
#          (default tree: build/local)
#
#  BY DEFAULT IT BUILDS FIRST. It has twice reported green over crashes that were
#  live in source, because ~/Library held components older than the code. Pass
#  --no-build only when you have just built and want the checks alone; the
#  freshness phase still runs, and still fails, if anything installed does not
#  match the build tree.
#
#  Exit 0 only if every phase passes.
# =============================================================================

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TREE="build/local"
BUILD_FIRST=1
for arg in "$@"; do
    case "$arg" in
        --no-build) BUILD_FIRST=0 ;;
        -*)         echo "unknown option: $arg" >&2; exit 2 ;;
        *)          TREE="$arg" ;;
    esac
done
case "$TREE" in
    /*) BUILD="$TREE" ;;
    *) BUILD="$ROOT/$TREE" ;;
esac
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"

fails=0
pass=0

hr() { printf '%s\n' "-------------------------------------------------------------"; }

# --- 0. BUILD WHAT WE ARE ABOUT TO VALIDATE ----------------------------------
#  This phase exists because the gate has now reported GREEN over two separate
#  crashes that were live in source at the time:
#
#    * a MIDI-learn constructor that segfaulted five plugins on instantiation;
#    * an instrument-rack buffer overflow on oversized blocks.
#
#  In both cases the .component bundles in ~/Library predated the source change,
#  so auval faithfully validated binaries nobody was shipping. A gate that tests
#  yesterday's build is worse than no gate: it converts "untested" into "passed".
#
#  JUCE targets carry COPY_PLUGIN_AFTER_BUILD, so building an _AU target installs
#  it. Build => install => validate is therefore one coherent chain. Only the AU
#  format is built here (not VST3/Standalone) because auval is the only binary
#  consumer in this script; use scripts/package_suite_installer.sh for the rest.
build_phase() {
    hr; echo "BUILD (AU + contracts)"; hr
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        echo "  FAIL  no CMake tree at $TREE - configure it first"
        fails=$((fails+1)); return
    fi

    # Ask the build system what exists rather than hardcoding a list that will
    # silently drift as products are added.
    local targets
    targets="$(ninja -C "$BUILD" -t targets all 2>/dev/null \
        | grep -E '^([A-Za-z0-9_]+(_AU|Contract)|AutoPitchRender|SongKeyRender): ' | cut -d: -f1 | sort -u)"
    if [ -z "$targets" ]; then
        echo "  FAIL  could not enumerate _AU/Contract targets from $TREE"
        fails=$((fails+1)); return
    fi

    local count; count="$(printf '%s\n' "$targets" | wc -l | tr -d ' ')"
    echo "  building $count targets (this is the slow part; --no-build to skip)"
    # shellcheck disable=SC2086
    if cmake --build "$BUILD" --target $(printf '%s ' $targets) >/tmp/sattari_gate_build.log 2>&1; then
        printf '  ok    build\n'; pass=$((pass+1))
    else
        printf '  FAIL  build\n'; fails=$((fails+1))
        grep -E "error:|FAILED" /tmp/sattari_gate_build.log | head -20 | sed 's/^/          /'
    fi
}

# --- 0b. FRESHNESS BACKSTOP --------------------------------------------------
#  Even with the build phase, an installed component can fail to match the tree:
#  installed by hand, built from another checkout, or left behind by a target that
#  failed while the rest succeeded. Verified directly, and treated as a HARD
#  failure rather than a warning — the whole point is that a stale pass looks
#  exactly like a real one.
#
#  It also catches DUPLICATE AU IDENTITIES, which are worse than staleness: macOS
#  keys the component registry on (type, subtype, manufacturer), so two bundles
#  claiming one identity means the host loads an undefined one of them. Renaming
#  Brain to Compass without deleting the old bundle produced exactly that.
#
#  Detail lives in scripts/check_component_freshness.py.
staleness_phase() {
    hr; echo "FRESHNESS (does the installed binary match this working tree?)"; hr
    if out="$(python3 "$ROOT/scripts/check_component_freshness.py" "$BUILD" 2>&1)"; then
        printf '  ok    %s\n' "$(printf '%s' "$out" | tail -1)"; pass=$((pass+1))
    else
        printf '  FAIL  component freshness\n'; fails=$((fails+1))
        printf '%s\n' "$out" | sed 's/^/          /' | head -20
    fi
}

# Every validation process uses a private session ID. Never move, replace, or
# clear the live session bus: an open DAW may be using it during validation.

[ $BUILD_FIRST -eq 1 ] && build_phase
staleness_phase

# --- 1. Contracts ------------------------------------------------------------
hr; echo "CONTRACTS"; hr
if python3 "$ROOT/scripts/run_release_contracts.py" "$BUILD"; then
    pass=$((pass+1))
else
    fails=$((fails+1))
fi

# --- 2. auval ----------------------------------------------------------------
hr; echo "auval (type read from each Info.plist)"; hr
#  Products marked lifecycle "excluded" in config/products.json are not part of
#  this release program and are not auval'd. They are announced, NOT dropped
#  silently: a hidden skip makes "N green" read as "everything validated" when
#  something was never tested, which is the same lie as validating a stale
#  binary. Driven by the catalog rather than a hardcoded name so the list cannot
#  drift away from the product data.
excluded_names="$(python3 -c "
import json
with open('$ROOT/config/products.json') as f: d=json.load(f)
print('\n'.join(p['name'] for p in d['products'] if p.get('lifecycle')=='excluded'))
" 2>/dev/null)"
skipped=0
for comp in "$COMPONENTS"/*.component; do
    [ -d "$comp" ] || continue
    plist="$comp/Contents/Info.plist"
    [ -f "$plist" ] || continue
    manu="$(plutil -extract AudioComponents.0.manufacturer raw "$plist" 2>/dev/null)"
    [ "$manu" = "Satr" ] || continue                       # only ours
    type="$(plutil -extract AudioComponents.0.type raw "$plist" 2>/dev/null)"
    sub="$(plutil -extract AudioComponents.0.subtype raw "$plist" 2>/dev/null)"
    nm="$(basename "$comp" .component)"
    if printf '%s\n' "$excluded_names" | grep -qxF "$nm"; then
        printf '  skip  %-28s %s %s   (lifecycle: excluded)\n' "$nm" "$type" "$sub"
        skipped=$((skipped+1)); continue
    fi
    if SATTARI_SESSION_ID="auval-$(uuidgen)" auval -v "$type" "$sub" "$manu" 2>&1 | grep -q "VALIDATION SUCCEEDED"; then
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
skip_note=""
[ "${skipped:-0}" -gt 0 ] && skip_note=", $skipped skipped (excluded products, NOT validated)"
if [ $fails -eq 0 ]; then
    echo "RELEASE CHECK PASSED — $pass checks green$skip_note"
    exit 0
fi
echo "RELEASE CHECK FAILED — $fails failing, $pass green$skip_note"
exit 1
