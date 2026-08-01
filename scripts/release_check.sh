#!/usr/bin/env bash
# =============================================================================
#  Sattari Suite — release gate
# -----------------------------------------------------------------------------
#  Runs everything that can fail a release, in one pass, and reports precisely
#  what broke. Three phases:
#
#    0. BUILD      — every _AU and *Contract target, so the things validated below
#                    are the things in the working tree (skip with --no-build).
#    0b. STALENESS — hard-fails if any installed .component predates the source.
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
#  Usage:  scripts/release_check.sh [build-tree] [--no-build]
#          (default tree: build/local)
#
#  BY DEFAULT IT BUILDS FIRST. It has twice reported green over crashes that were
#  live in source, because ~/Library held components older than the code. Pass
#  --no-build only when you have just built and want the checks alone; the
#  staleness phase still runs, and still fails, if anything installed is older
#  than the working tree.
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
BUILD="$ROOT/$TREE"
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
        | grep -E '^[A-Za-z0-9_]+(_AU|Contract): ' | cut -d: -f1 | sort -u)"
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
    if out="$(python3 "$ROOT/scripts/check_component_freshness.py" 2>&1)"; then
        printf '  ok    %s\n' "$(printf '%s' "$out" | tail -1)"; pass=$((pass+1))
    else
        printf '  FAIL  component freshness\n'; fails=$((fails+1))
        printf '%s\n' "$out" | sed 's/^/          /' | head -20
    fi
}

# --- 0. Make the contracts hermetic -----------------------------------------
#  The contracts are NOT self-contained: any plugin that follows the cross-plugin
#  TEMPO bus (Echo, Space, Pulse, Riser, Warp, Granulator, Side Chain Master...)
#  falls back to it when the host reports no tempo — and a contract harness has no
#  playhead, so the bus ALWAYS wins there.
#
#  That makes the suite order-dependent in a way that looks like a DSP bug: auval
#  instantiating Rhythm Genie publishes a live BPM to the bus, and a contract run
#  afterwards then measures a 1/8 note at THAT tempo instead of the 120 it assumes.
#  Observed: Echo failed 4 checks inside the gate while passing standalone.
#
#  JUCE resolves the bus path through getpwuid, NOT $HOME, so redirecting HOME does
#  not isolate it. The directory has to be moved aside for real, and restored no
#  matter how we exit.
#
#  Stashing ONCE is not enough: 19 of the contracts touch the bus themselves and
#  several PUBLISH to it (SattariCrossPluginBus, Brain, AutoPitch, Comp...), so a
#  contract that runs later reads what an earlier one wrote. The bus is therefore
#  cleared before EVERY contract, which is the only way to make each one measure
#  its own DSP rather than its predecessor's side effects.
BUS_DIR="$HOME/Library/Sattari Audio/cross-plugin"
BUS_STASH="$(mktemp -d)/cross-plugin"
restore_bus() {
    if [ -d "$BUS_STASH" ]; then
        rm -rf "$BUS_DIR"                       # drop anything the run created
        mkdir -p "$(dirname "$BUS_DIR")"
        mv "$BUS_STASH" "$BUS_DIR"
    fi
}
trap restore_bus EXIT INT TERM
if [ -d "$BUS_DIR" ]; then
    mv "$BUS_DIR" "$BUS_STASH"
    echo "cross-plugin bus stashed for the contract run (restored on exit)"
fi

[ $BUILD_FIRST -eq 1 ] && build_phase
staleness_phase

# --- 1. Contracts ------------------------------------------------------------
hr; echo "CONTRACTS"; hr
while IFS= read -r exe; do
    name="$(basename "$exe")"
    rm -rf "$BUS_DIR"          # see above: per-contract, not once per run
    out="$("$exe" 2>&1)"
    rc=$?
    last="$(printf '%s' "$out" | tail -1)"
    if [ $rc -eq 0 ]; then
        printf '  ok    %-42s %s\n' "$name" "$last"; pass=$((pass+1))
    else
        printf '  FAIL  %-42s %s\n' "$name" "$last"; fails=$((fails+1))
        printf '%s\n' "$out" | sed 's/^/          /' | tail -12
    fi
done < <(find "$BUILD" -type f -perm -111 -name '*Contract' \
    ! -path '*/plugins/stemdeck/*' 2>/dev/null | sort)

# --- 2. auval ----------------------------------------------------------------
#  Restore the bus BEFORE auval: auval exercises the real plugins, and a plugin
#  that publishes to the bus should be publishing to the real one.
restore_bus
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
    [ "$nm" = "Sattari StemDeck" ] && continue
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
