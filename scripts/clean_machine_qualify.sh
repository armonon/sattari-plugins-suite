#!/bin/bash
# =============================================================================
#  Sattari Audio - clean-machine qualification (Sattari Auto Pitch + Sattari Key)
# -----------------------------------------------------------------------------
#  Runs ON a clean Mac or macOS VM against the artefact under test: a .pkg, a
#  .dmg, or a folder/archive of bundles (e.g. the tester kit). Every check is
#  recorded as PASS / FAIL / SKIP (plus WARN / MANUAL / INFO notes) with its
#  evidence log in a timestamped results folder, and summarised in summary.md.
#
#  Phases
#    preflight   macOS version, arch, SIP, Gatekeeper, VM, dev-tool contamination;
#                proves NO Sattari plug-ins, presets, support files or receipts
#                exist yet (every path checked is listed). Stops if any are found.
#    inventory   finds the Sattari bundles inside the artefact; records sha256 and
#                a signature-independent sha256 (Mach-O with the signature
#                removed) so a Developer-ID-signed binary can still be tied to the
#                frozen candidate build (compare with --expect-fingerprints).
#    signing     codesign --verify --deep --strict, Developer ID + Team ID,
#                hardened runtime, timestamp, spctl assessment, stapled ticket,
#                pkgutil --check-signature for a pkg, universal architecture.
#    quarantine  copies the artefact, adds com.apple.quarantine as a browser
#                download would, and asks Gatekeeper. Prints the manual UI step.
#    install     `sudo installer -pkg ... -target /` for a pkg, else copies the
#                bundles. Before/after snapshots give the exact manifest of what
#                the install created. Verifies AU/VST3/Standalone(/AAX) paths,
#                installed sha256, permissions, factory presets, runtime files.
#    discovery   auval -a listing, auval -v per AU (type/subtype/manufacturer
#                read from the installed Info.plist), VST3 sanity, pluginval when
#                present (not required), installed DAWs + manual steps.
#    uninstall   removes exactly the manifest (nothing else), verifies nothing
#                installed remains and the AU registry no longer lists it.
#    reinstall   installs again, re-verifies files and auval, and leaves the
#                plug-ins installed for the manual DAW checklist.
#
#  Safety
#    * --dry-run executes only read-only inspection (and writes its own results
#      folder); every install/copy/remove/xattr/sudo/registrar action is printed.
#    * Uninstall only removes paths recorded in a manifest written by this script,
#      each re-checked (Sattari name, com.sattari bundle id, standard location,
#      not a symlink, unchanged since install). Never touches anything else.
#    * --root DIR installs into a fake root (DIR/Library/..., DIR$HOME/Library/...)
#      for testing the script itself; auval/pkg receipts/pluginval are skipped.
#
#  Usage
#    scripts/clean_machine_qualify.sh --pkg Sattari-Audio-Suite-signed.pkg
#    scripts/clean_machine_qualify.sh --bundles ~/Downloads/sattari-tester-kit
#    scripts/clean_machine_qualify.sh --dmg Sattari.dmg --expect-team ABCDE12345
#    scripts/clean_machine_qualify.sh --uninstall-from RESULTS/manifest-reinstall.tsv
#    scripts/clean_machine_qualify.sh --fingerprint-only --bundles <Release dir>
#
#  Exit: 0 no FAIL, 1 at least one FAIL, 2 usage error, 3 preflight found an
#  existing Sattari install (machine is not clean; nothing was changed).
#  Written for the stock /bin/bash 3.2 and only macOS-native tools, so it runs on
#  a machine without Xcode Command Line Tools (stapler is skipped there).
# =============================================================================
# `cond && rec PASS || rec FAIL` is used deliberately: rec() ends in printf and always returns 0.
# shellcheck disable=SC2015
set -u -o pipefail
umask 022
export LC_ALL=C

SCRIPT_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
DAW_CHECKLIST="validation/daw/2026-09-25-daw-qualification-checklist.md"
CM_CHECKLIST="validation/clean-machine/2026-09-25-clean-machine-checklist.md"

usage() {
    awk 'NR>2 && /^# =====/ {exit} NR>2 {sub(/^# ? ?/, ""); print}' "$SCRIPT_PATH"
    cat <<'EOF'

Options
  --pkg FILE              installer package to qualify (repeatable)
  --dmg FILE              disk image containing a pkg or bundles (repeatable)
  --bundles DIR|ARCHIVE   folder, .zip or .tar.gz containing bundles (repeatable)
  --products LIST         comma list of auto-pitch,song-key (default: both)
  --scope user|system     bundle-copy destination (default user: ~/Library, ~/Applications)
  --expect-team TEAMID    FAIL unless every signature carries this Team ID
  --expect-fingerprints F fingerprints.tsv from --fingerprint-only on the candidate
  --candidate LABEL       free text recorded in the summary (e.g. cbd1ce4)
  --out DIR               where the results folder is created (default: ./sattari-clean-machine-results)
  --root DIR              fake install root, for testing this script only
  --dry-run               inspect and print actions; change nothing outside the results folder
  --no-pluginval          do not run pluginval even if it is installed
  --no-reinstall          stop after the uninstall phase
  --final-uninstall       remove the reinstall too (default: leave it for DAW tests)
  --non-interactive       never prompt (a Hub-driven install then FAILs instead of waiting)
  --uninstall-from FILE   only run the uninstall phase against a manifest from an earlier run
  --fingerprint-only      only inventory the artefact and write fingerprints.tsv
  --auval-timeout SECS    per-AU auval timeout (default 300)
  -h, --help
EOF
}

# ----------------------------------------------------------------------------- args
DRY_RUN=0; ROOT=""; SCOPE="user"; PRODUCTS="auto-pitch,song-key"
EXPECT_TEAM=""; EXPECT_FP=""; CANDIDATE=""; OUT_BASE="$PWD/sattari-clean-machine-results"
NO_PLUGINVAL=0; NO_REINSTALL=0; FINAL_UNINSTALL=0; NONINTERACTIVE=0
UNINSTALL_FROM=""; FINGERPRINT_ONLY=0; AUVAL_TIMEOUT=300; PLUGINVAL_TIMEOUT=900
PKGS=(); DMGS=(); BSRCS=()

need_arg() { [ $# -ge 2 ] && [ -n "$2" ] || { echo "option $1 needs a value" >&2; exit 2; }; }
abspath() { case "$1" in /*) printf '%s\n' "$1" ;; *) printf '%s/%s\n' "$PWD" "$1" ;; esac; }
while [ $# -gt 0 ]; do
    case "$1" in
        --pkg)          need_arg "$@"; PKGS+=("$(abspath "$2")"); shift 2 ;;
        --dmg)          need_arg "$@"; DMGS+=("$(abspath "$2")"); shift 2 ;;
        --bundles)      need_arg "$@"; BSRCS+=("$(abspath "$2")"); shift 2 ;;
        --products)     need_arg "$@"; PRODUCTS="$2"; shift 2 ;;
        --scope)        need_arg "$@"; SCOPE="$2"; shift 2 ;;
        --expect-team)  need_arg "$@"; EXPECT_TEAM="$2"; shift 2 ;;
        --expect-fingerprints) need_arg "$@"; EXPECT_FP="$(abspath "$2")"; shift 2 ;;
        --candidate)    need_arg "$@"; CANDIDATE="$2"; shift 2 ;;
        --out)          need_arg "$@"; OUT_BASE="$(abspath "$2")"; shift 2 ;;
        --root)         need_arg "$@"; ROOT="$(abspath "$2")"; shift 2 ;;
        --dry-run)      DRY_RUN=1; shift ;;
        --no-pluginval) NO_PLUGINVAL=1; shift ;;
        --no-reinstall) NO_REINSTALL=1; shift ;;
        --final-uninstall) FINAL_UNINSTALL=1; shift ;;
        --non-interactive) NONINTERACTIVE=1; shift ;;
        --uninstall-from) need_arg "$@"; UNINSTALL_FROM="$(abspath "$2")"; shift 2 ;;
        --fingerprint-only) FINGERPRINT_ONLY=1; shift ;;
        --auval-timeout) need_arg "$@"; AUVAL_TIMEOUT="$2"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *)              echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || { echo "macOS only" >&2; exit 2; }
case "$SCOPE" in user|system) ;; *) echo "--scope must be user or system" >&2; exit 2 ;; esac
ROOT="${ROOT%/}"
if [ -n "$ROOT" ]; then
    case "$ROOT" in /|/System*|/Library*|/Applications*|/Users|/usr*|/private/var/db*)
        echo "--root must be a scratch directory, not $ROOT" >&2; exit 2 ;; esac
fi
n_inputs=$(( ${#PKGS[@]} + ${#DMGS[@]} + ${#BSRCS[@]} ))
if [ -z "$UNINSTALL_FROM" ] && [ "$n_inputs" -eq 0 ]; then
    echo "give at least one of --pkg, --dmg, --bundles (or --uninstall-from); see --help" >&2; exit 2
fi
for f in ${PKGS[@]+"${PKGS[@]}"} ${DMGS[@]+"${DMGS[@]}"} ${BSRCS[@]+"${BSRCS[@]}"} ${UNINSTALL_FROM:+"$UNINSTALL_FROM"} ${EXPECT_FP:+"$EXPECT_FP"}; do
    [ -e "$f" ] || { echo "not found: $f" >&2; exit 2; }
done
SEL_PRODUCTS="$(printf '%s' "$PRODUCTS" | tr ',' ' ')"
for p in $SEL_PRODUCTS; do
    case "$p" in auto-pitch|song-key) ;; *) echo "unknown product id: $p (auto-pitch, song-key)" >&2; exit 2 ;; esac
done

# ----------------------------------------------------------------------------- products
#  Sattari Key has no factory presets and is deliberately model-free (no ONNX);
#  Auto Pitch embeds CREPE and ONNX Runtime 1.22.0 in every bundle.
prod_name()    { case "$1" in auto-pitch) echo "Sattari Auto Pitch" ;; song-key) echo "Sattari Key" ;; esac; }
prod_subtype() { case "$1" in auto-pitch) echo "Ap01" ;; song-key) echo "Sk01" ;; esac; }
prod_formats() { echo "AU VST3 Standalone"; }
prod_onnx()    { case "$1" in auto-pitch) echo required ;; song-key) echo forbidden ;; esac; }
prod_factory() { case "$1" in auto-pitch) echo "Pitch Center" ;; *) echo "" ;; esac; }
prod_presetdir() { case "$1" in auto-pitch) echo auto-pitch ;; song-key) echo song-key ;; esac; }
name_to_prod() {
    case "$1" in "Sattari Auto Pitch") echo auto-pitch ;; "Sattari Key") echo song-key ;; *) echo "" ;; esac
}
MANU="Satr"

# ----------------------------------------------------------------------------- locations
HOME_R="$ROOT$HOME"
SYS_COMP="$ROOT/Library/Audio/Plug-Ins/Components"
SYS_VST3="$ROOT/Library/Audio/Plug-Ins/VST3"
SYS_AAX="$ROOT/Library/Application Support/Avid/Audio/Plug-Ins"
SYS_APPS="$ROOT/Applications"
USR_COMP="$HOME_R/Library/Audio/Plug-Ins/Components"
USR_VST3="$HOME_R/Library/Audio/Plug-Ins/VST3"
USR_APPS="$HOME_R/Applications"
# JUCE userApplicationDataDirectory is ~/Library on macOS: presets, models and the
# cross-plugin bus live in ~/Library/Sattari Audio. The tester kit's backups and the
# SattariPresetManager.h comment use ~/Library/Application Support/Sattari Audio.
SUP_USER_LIB="$HOME_R/Library/Sattari Audio"
SUP_USER_AS="$HOME_R/Library/Application Support/Sattari Audio"
SUP_SYS_AS="$ROOT/Library/Application Support/Sattari Audio"

bundle_dirs() {   # directories that hold installable bundles (one level deep)
    printf '%s\n' "$SYS_COMP" "$SYS_VST3" "$SYS_AAX" "$SYS_APPS" "$USR_COMP" "$USR_VST3" "$USR_APPS"
}
support_dirs() { printf '%s\n' "$SUP_USER_LIB" "$SUP_USER_AS" "$SUP_SYS_AS"; }
preset_dirs()  { printf '%s\n' "$HOME_R/Library/Audio/Presets" "$ROOT/Library/Audio/Presets"; }
pref_dirs()    {
    printf '%s\n' "$HOME_R/Library/Preferences" "$HOME_R/Library/Caches" \
        "$HOME_R/Library/Saved Application State" "$HOME_R/Library/Containers" \
        "$HOME_R/Library/HTTPStorages" "$HOME_R/Library/WebKit"
}

# ----------------------------------------------------------------------------- results
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
HOSTTAG="$(scutil --get LocalHostName 2>/dev/null || hostname -s)"
MODE_TAG=""; [ "$DRY_RUN" = 1 ] && MODE_TAG="-dryrun"
RES="$OUT_BASE/$STAMP-$HOSTTAG$MODE_TAG"
WORK="$RES/work"
mkdir -p "$RES/evidence" "$WORK" || { echo "cannot create results folder $RES" >&2; exit 2; }
: > "$RES/results.tsv"; : > "$RES/actions.log"; : > "$RES/manual-steps.md"
exec > >(tee -a "$RES/console.log") 2>&1

ev() {  # allocate (and create) a numbered evidence file; state lives on disk so $(ev x) works
    local n f
    n=$(( $(find "$RES/evidence" -type f | wc -l) + 1 ))
    f="$(printf '%s/evidence/%03d-%s.log' "$RES" "$n" "$(printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '_' | cut -c1-80)")"
    : > "$f"; printf '%s' "$f"
}
clean1() { printf '%s' "$*" | tr '\t\n\r' '   ' | cut -c1-400; }
rec() {  # phase check status detail [evidence-file]
    local e="${5:-}"; e="${e#"$RES"/}"
    printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$(clean1 "$2")" "$3" "$(clean1 "$4")" "$e" >> "$RES/results.tsv"
    printf '  %-6s %-11s %s: %s\n' "$3" "[$1]" "$2" "$4"
}
manual() { printf -- '- **%s**: %s\n' "$1" "$2" >> "$RES/manual-steps.md"; rec "$1" "manual step" MANUAL "$2"; }
hdr() { printf '\n== %s ==\n' "$1"; }
run_ev() {  # evidence-file cmd...  (read-only commands; output appended)
    local f="$1"; shift
    { printf '$'; printf ' %q' "$@"; printf '\n'; } >> "$f"
    "$@" >> "$f" 2>&1
    local rc=$?
    printf '[exit %d]\n' "$rc" >> "$f"
    return $rc
}
act() {  # evidence-file cmd...  (anything that changes the machine)
    local f="$1"; shift
    local line; line="$(printf ' %q' "$@")"
    if [ "$DRY_RUN" = 1 ]; then
        printf '    DRY-RUN would run:%s\n' "$line"
        printf 'DRY-RUN (not executed):%s\n' "$line" >> "$RES/actions.log"
        printf 'DRY-RUN (not executed):%s\n' "$line" >> "$f"
        return 0
    fi
    printf '%s  RUN:%s\n' "$(date -u +%H:%M:%S)" "$line" >> "$RES/actions.log"
    run_ev "$f" "$@"
}
run_timeout() {  # secs evidence-file cmd...   (bash 3.2 has no timeout(1))
    local secs="$1" f="$2"; shift 2
    { printf '$ [timeout %ss]' "$secs"; printf ' %q' "$@"; printf '\n'; } >> "$f"
    "$@" >> "$f" 2>&1 &
    local pid=$!
    ( sleep "$secs"; kill -TERM "$pid" 2>/dev/null; sleep 5; kill -KILL "$pid" 2>/dev/null ) >/dev/null 2>&1 &
    local watchdog=$!
    wait "$pid"; local rc=$?
    kill "$watchdog" 2>/dev/null; wait "$watchdog" 2>/dev/null
    printf '[exit %d]\n' "$rc" >> "$f"
    return $rc
}

auval_list() {  # evidence -> 0 only if `auval -a` completed and printed a component list
    run_timeout 180 "$1" auval -a || return 1
    grep -qE '^(aufx|aumf|aumu|augn|aumi|auou|aufc|aupn|aumx)[[:space:]]' "$1"
}
AUVAL_HANG="auval -a did not finish within 180 s (it needs a logged-in GUI session: run from Terminal, not ssh)"

REAL=1; [ -n "$ROOT" ] && REAL=0
HAVE_CLT=0; { xcode-select -p >/dev/null 2>&1 && [ -d "$(xcode-select -p 2>/dev/null)" ]; } && HAVE_CLT=1
[ "${SATTARI_CM_ASSUME_NO_CLT:-0}" = 1 ] && HAVE_CLT=0   # test hook: exercise the stock-Mac code path
SUDO_OK=0
need_sudo() {  # obtain sudo once, only for real, non-dry-run system changes
    [ "$REAL" = 1 ] && [ "$DRY_RUN" = 0 ] || return 0
    [ "$SUDO_OK" = 1 ] && return 0
    echo "  sudo is needed for a system-level install/uninstall step; you may be asked for your password."
    sudo -v && SUDO_OK=1
}

cleanup() {
    local m
    for m in "$WORK"/mnt-*; do
        [ -d "$m" ] && hdiutil detach -quiet "$m" >/dev/null 2>&1
    done
    rm -rf "$WORK" 2>/dev/null
}
trap cleanup EXIT
trap 'echo "interrupted"; exit 130' INT TERM

# ----------------------------------------------------------------------------- helpers
pl() { plutil -extract "$1" raw -o - "$2" 2>/dev/null; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
bundle_exec() { local e; e="$(pl CFBundleExecutable "$1/Contents/Info.plist")"; [ -n "$e" ] && printf '%s/Contents/MacOS/%s\n' "$1" "$e"; }
bundle_id() { pl CFBundleIdentifier "$1/Contents/Info.plist"; }
bundle_fmt() {
    case "$1" in
        *.component) echo AU ;; *.vst3) echo VST3 ;; *.aaxplugin) echo AAX ;; *.app) echo Standalone ;; *) echo "" ;;
    esac
}
is_sattari_name() { case "$(lower "$(basename "$1")")" in sattari*|com.sattari*) return 0 ;; esac; return 1; }
sha() { shasum -a 256 "$1" 2>/dev/null | awk '{print $1}'; }
unsigned_sha() {  # sha256 of a Mach-O with its code signature removed (stable across re-signing)
    local tmp="$WORK/unsign.$$"
    cp "$1" "$tmp" 2>/dev/null || { echo "-"; return; }
    codesign --remove-signature "$tmp" >/dev/null 2>&1
    sha "$tmp"; rm -f "$tmp"
}
macho_files() {  # every Mach-O inside a bundle
    local f
    find "$1/Contents" -type f \( -path '*/MacOS/*' -o -path '*/Frameworks/*' -o -name '*.dylib' -o -name '*.so' \) 2>/dev/null | sort |
        while IFS= read -r f; do file -b "$f" 2>/dev/null | grep -q 'Mach-O' && printf '%s\n' "$f"; done
}
fingerprint_bundle() {  # bundle -> lines: rel \t sha256 \t unsigned-sha256
    local b="$1" f rel
    macho_files "$b" | while IFS= read -r f; do
        rel="$(basename "$b")/${f#"$b"/}"
        printf '%s\t%s\t%s\n' "$rel" "$(sha "$f")" "$(unsigned_sha "$f")"
    done
}
find_bundles() {  # plug-in bundles are pruned; .app is not (the Hub app carries payloads)
    find "$1" \( -name '*.component' -o -name '*.vst3' -o -name '*.aaxplugin' \) -type d -print -prune \
        -o -name '*.app' -type d -print 2>/dev/null | sort
}
archs_of() { file -b "$1" 2>/dev/null | grep -oE 'arm64e?|x86_64' | sort -u | tr '\n' ' ' | sed 's/ $//'; }
sattari_scan() {  # everything Sattari-looking in all watched places (one path per line)
    local d
    bundle_dirs | while IFS= read -r d; do
        [ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 1 \( -iname '*sattari*' -o -iname 'autopitch.*' \) 2>/dev/null
    done
    support_dirs | while IFS= read -r d; do [ -e "$d" ] && find "$d" 2>/dev/null; done
    preset_dirs | while IFS= read -r d; do [ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 2 -iname '*sattari*' 2>/dev/null; done
    pref_dirs | while IFS= read -r d; do [ -d "$d" ] && find "$d" -mindepth 1 -maxdepth 1 -iname '*sattari*' 2>/dev/null; done
}
snapshot() { sattari_scan | sort -u > "$1"; }

# ============================================================================= PREFLIGHT
preflight() {
    hdr "PREFLIGHT"
    local e; e="$(ev preflight-environment)"
    run_ev "$e" sw_vers; run_ev "$e" uname -a; run_ev "$e" sysctl -n hw.model
    run_ev "$e" sysctl -n machdep.cpu.brand_string; run_ev "$e" sysctl -n kern.hv_vmm_present
    run_ev "$e" csrutil status; run_ev "$e" spctl --status; run_ev "$e" xcode-select -p
    local osv arch sip gk vm
    osv="$(sw_vers -productVersion) ($(sw_vers -buildVersion))"; arch="$(uname -m)"
    sip="$(csrutil status 2>/dev/null | sed 's/.*status: //; s/\.$//')"
    gk="$(spctl --status 2>/dev/null)"
    vm="$(sysctl -n kern.hv_vmm_present 2>/dev/null)"; [ "$vm" = 1 ] && vm="yes (virtual machine)" || vm="no"
    local rosetta="not installed"; [ -e /Library/Apple/usr/libexec/oah/libRosettaRuntime ] && rosetta="installed"
    [ "$arch" = x86_64 ] && rosetta="n/a (Intel)"
    rec preflight "macOS" INFO "$osv; arch $arch; model $(sysctl -n hw.model 2>/dev/null); VM: $vm; Rosetta: $rosetta" "$e"
    case "$(sw_vers -productVersion)" in
        1[0-2].*|13.[0-2]|13.[0-2].*) rec preflight "minimum macOS 13.3" FAIL "bundles declare macOS 13.3+; this is $osv" "$e" ;;
        *) rec preflight "minimum macOS 13.3" PASS "$osv" "$e" ;;
    esac
    case "$sip" in *enabled*) rec preflight "SIP" PASS "$sip" "$e" ;; *) rec preflight "SIP" WARN "$sip (customer machines run with SIP enabled)" "$e" ;; esac
    case "$gk" in *enabled*) rec preflight "Gatekeeper" PASS "$gk" "$e" ;;
        *) rec preflight "Gatekeeper" FAIL "$gk: assessments disabled, so Gatekeeper results would be meaningless (sudo spctl --global-enable)" "$e" ;; esac
    if [ "$HAVE_CLT" = 1 ]; then
        rec preflight "developer tools" WARN "Command Line Tools present at $(xcode-select -p); not a stock customer machine (stapler checks will run)" "$e"
    else
        rec preflight "developer tools" PASS "no Command Line Tools (stock machine; stapler checks will SKIP, spctl/pkgutil cover notarization)" "$e"
    fi
    local contam="" p
    for p in /opt/homebrew /usr/local/Cellar /usr/local/lib/libonnxruntime.dylib /opt/homebrew/lib/libonnxruntime.dylib /usr/local/lib/librubberband.dylib; do
        [ -e "$p" ] && contam="$contam $p"
    done
    if [ -n "$contam" ]; then rec preflight "dev libraries" WARN "present:$contam (could mask a missing embedded dependency)" "$e"
    else rec preflight "dev libraries" PASS "no Homebrew / system ONNX Runtime / Rubber Band" "$e"; fi
    [ "$REAL" = 0 ] && rec preflight "install root" INFO "FAKE ROOT $ROOT (test mode: auval, receipts, pluginval skipped)" "$e"

    # --- the machine must not already have Sattari anything
    local paths; paths="$(ev preflight-paths-checked)"
    {
        echo "# Every location checked for pre-existing Sattari files (case-insensitive *sattari*, legacy AutoPitch.*)"
        echo "# bundle folders (one level):"; bundle_dirs
        echo "# support folders (must not exist):"; support_dirs
        echo "# preset folders (two levels):"; preset_dirs
        echo "# preferences / caches / saved state / containers (one level):"; pref_dirs
    } >> "$paths"
    local found; found="$(sattari_scan | sort -u)"
    { echo "# found:"; printf '%s\n' "${found:-<none>}"; } >> "$paths"
    local dirty=0
    if [ -n "$found" ]; then
        dirty=1
        rec preflight "no Sattari files" FAIL "$(printf '%s\n' "$found" | wc -l | tr -d ' ') existing Sattari path(s): $(printf '%s' "$found" | head -5 | tr '\n' ';')" "$paths"
    else
        rec preflight "no Sattari files" PASS "$(bundle_dirs | wc -l | tr -d ' ') plug-in/app folders, 3 support, 2 preset, 6 preference folders checked; none found" "$paths"
    fi
    if [ "$REAL" = 1 ]; then
        local r; r="$(ev preflight-receipts)"; run_ev "$r" pkgutil --pkgs
        local rc; rc="$(pkgutil --pkgs 2>/dev/null | grep -i sattari)"
        if [ -n "$rc" ]; then dirty=1; rec preflight "no Sattari pkg receipts" FAIL "$(printf '%s' "$rc" | tr '\n' ' ')" "$r"
        else rec preflight "no Sattari pkg receipts" PASS "pkgutil --pkgs has none" "$r"; fi
        local a; a="$(ev preflight-auval-a)"
        local reg=""
        if ! auval_list "$a"; then
            rec preflight "no registered Satr AUs" FAIL "$AUVAL_HANG; the AU registry could not be checked" "$a"
        elif reg="$(grep -E "[[:space:]]${MANU}[[:space:]]" "$a")" && [ -n "$reg" ]; then
            dirty=1; rec preflight "no registered Satr AUs" FAIL "$(printf '%s' "$reg" | head -3 | tr '\n' ';')" "$a"
        else rec preflight "no registered Satr AUs" PASS "auval -a lists no manufacturer $MANU" "$a"; fi
    else
        rec preflight "pkg receipts / AU registry" SKIP "fake root" ""
    fi
    if [ "$dirty" = 1 ]; then
        if [ "$DRY_RUN" = 1 ]; then
            echo "  preflight: machine is NOT clean. DRY-RUN continues to show the plan; a real run stops here."
        else
            echo "  preflight: machine is NOT clean. Stopping before changing anything."
            echo "  Use a fresh macOS VM/user, or remove the listed paths yourself, then re-run."
            PREFLIGHT_ABORT=1
        fi
    fi
}

# ============================================================================= INVENTORY
INV_PATH=(); INV_PROD=(); INV_FMT=(); INV_ORIGIN=()
CONTAINERS=()     # pkg/dmg/archive files given by the user
PKG_TO_INSTALL=() # pkgs (possibly inside a mounted dmg)
BUNDLE_INSTALL=0

add_bundles_from() {  # dir origin
    local b fmt name prod
    while IFS= read -r b; do
        [ -f "$b/Contents/Info.plist" ] || continue
        fmt="$(bundle_fmt "$b")"
        name="$(pl CFBundleName "$b/Contents/Info.plist")"; [ -n "$name" ] || name="$(basename "${b%.*}")"
        prod="$(name_to_prod "$name")"
        [ -n "$prod" ] || prod="$(name_to_prod "$(basename "${b%.*}")")"
        if [ -z "$prod" ]; then
            is_sattari_name "$b" && rec inventory "ignored bundle" INFO "$(basename "$b") (not Auto Pitch / Key)" ""
            continue
        fi
        case " $SEL_PRODUCTS " in *" $prod "*) ;; *) rec inventory "ignored bundle" INFO "$(basename "$b") (product not selected)" ""; continue ;; esac
        INV_PATH+=("$b"); INV_PROD+=("$prod"); INV_FMT+=("$fmt"); INV_ORIGIN+=("$2")
    done < <(find_bundles "$1")
}

inventory() {
    hdr "INVENTORY"
    local e; e="$(ev inventory)"
    local i=0 f d
    for f in ${PKGS[@]+"${PKGS[@]}"}; do
        i=$((i + 1)); CONTAINERS+=("$f"); PKG_TO_INSTALL+=("$f")
        run_ev "$e" shasum -a 256 "$f"
        rec inventory "input pkg" INFO "$(basename "$f") sha256 $(sha "$f") $(stat -f %z "$f") bytes" "$e"
        d="$WORK/pkg-$i"
        if run_ev "$e" pkgutil --expand-full "$f" "$d"; then add_bundles_from "$d" "pkg:$(basename "$f")"
        else rec inventory "expand pkg" FAIL "pkgutil --expand-full failed for $(basename "$f")" "$e"; fi
        run_ev "$e" pkgutil --payload-files "$f" >/dev/null
    done
    for f in ${DMGS[@]+"${DMGS[@]}"}; do
        i=$((i + 1)); CONTAINERS+=("$f")
        run_ev "$e" shasum -a 256 "$f"
        rec inventory "input dmg" INFO "$(basename "$f") sha256 $(sha "$f") $(stat -f %z "$f") bytes" "$e"
        d="$WORK/mnt-$i"
        if [ "$DRY_RUN" = 1 ]; then
            act "$e" hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$d" "$f"
            rec inventory "dmg contents" SKIP "dry-run does not mount disk images" "$e"; continue
        fi
        mkdir -p "$d"
        if run_ev "$e" hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$d" "$f"; then
            local p n=0
            while IFS= read -r p; do
                n=$((n + 1)); PKG_TO_INSTALL+=("$p")
                run_ev "$e" pkgutil --expand-full "$p" "$WORK/dmgpkg-$i-$n" && add_bundles_from "$WORK/dmgpkg-$i-$n" "pkg:$(basename "$p")"
            done < <(find "$d" -maxdepth 2 -name '*.pkg' 2>/dev/null)
            [ "$n" -eq 0 ] && { add_bundles_from "$d" "dmg:$(basename "$f")"; BUNDLE_INSTALL=1; }
        else
            rec inventory "mount dmg" FAIL "hdiutil attach failed for $(basename "$f")" "$e"
        fi
    done
    for f in ${BSRCS[@]+"${BSRCS[@]}"}; do
        i=$((i + 1)); BUNDLE_INSTALL=1
        if [ -f "$f" ]; then
            CONTAINERS+=("$f")
            rec inventory "input archive" INFO "$(basename "$f") sha256 $(sha "$f")" "$e"
            d="$WORK/archive-$i"; mkdir -p "$d"
            case "$f" in
                *.zip) run_ev "$e" ditto -x -k "$f" "$d" ;;
                *.tar.gz|*.tgz|*.tar) run_ev "$e" tar -xf "$f" -C "$d" ;;
                *) rec inventory "archive" FAIL "unsupported archive type: $(basename "$f")" "$e"; continue ;;
            esac
            add_bundles_from "$d" "archive:$(basename "$f")"
        else
            add_bundles_from "$f" "dir:$f"
        fi
    done
    if [ "${#PKG_TO_INSTALL[@]}" -gt 0 ] && [ "$BUNDLE_INSTALL" = 1 ]; then
        rec inventory "install method" FAIL "give either installer packages or bundles, not both" ""
    fi

    local fp="$RES/fingerprints-source.tsv"
    printf '# relpath\tsha256\tsha256-signature-removed\n' > "$fp"
    local k
    for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
        fingerprint_bundle "${INV_PATH[$k]}" >> "$fp"
        printf '%s\t%s\t%s\t%s\n' "${INV_PROD[$k]}" "${INV_FMT[$k]}" "${INV_ORIGIN[$k]}" "${INV_PATH[$k]#"$WORK"/}" >> "$e"
    done
    local p fmt found
    for p in $SEL_PRODUCTS; do
        for fmt in $(prod_formats "$p"); do
            found=""
            for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
                [ "${INV_PROD[$k]}" = "$p" ] && [ "${INV_FMT[$k]}" = "$fmt" ] && found="${INV_PATH[$k]}"
            done
            if [ -n "$found" ]; then
                rec inventory "$(prod_name "$p") $fmt" PASS "$(basename "$found") v$(pl CFBundleShortVersionString "$found/Contents/Info.plist")" "$e"
            else
                rec inventory "$(prod_name "$p") $fmt" FAIL "not present in the provided artefact(s)" "$e"
            fi
        done
    done
    for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
        [ "${INV_FMT[$k]}" = AAX ] && rec inventory "$(prod_name "${INV_PROD[$k]}") AAX" INFO "AAX bundle present (optional format)" "$e"
    done
    rec inventory "source fingerprints" INFO "$(grep -vc '^#' "$fp") Mach-O files -> fingerprints-source.tsv" "$fp"
    compare_fingerprints "$fp" inventory
}

compare_fingerprints() {  # tsv phase
    [ -n "$EXPECT_FP" ] || return 0
    local rel s u exp_u miss=0 bad=0 ok=0 e; e="$(ev "$2-fingerprint-compare")"
    while IFS="$(printf '\t')" read -r rel s u; do
        case "$rel" in '#'*|'') continue ;; esac
        exp_u="$(awk -F'\t' -v r="$rel" '$1==r {print $3}' "$EXPECT_FP" | head -1)"
        if [ -z "$exp_u" ]; then miss=$((miss + 1)); echo "NOT IN REFERENCE: $rel" >> "$e"
        elif [ "$exp_u" = "$u" ]; then ok=$((ok + 1)); echo "match: $rel $u" >> "$e"
        else bad=$((bad + 1)); echo "MISMATCH: $rel got $u expected $exp_u" >> "$e"; fi
    done < "$1"
    if [ "$bad" -gt 0 ] || [ "$ok" -eq 0 ]; then
        rec "$2" "matches candidate fingerprints" FAIL "$ok match, $bad mismatch, $miss not in reference" "$e"
    elif [ "$miss" -gt 0 ]; then
        rec "$2" "matches candidate fingerprints" WARN "$ok match, $miss not in reference" "$e"
    else
        rec "$2" "matches candidate fingerprints" PASS "$ok/$ok Mach-O files identical to the candidate (signature removed)" "$e"
    fi
}

# ============================================================================= SIGNING
TEAMS=""
note_team() { [ -n "$1" ] && case " $TEAMS " in *" $1 "*) ;; *) TEAMS="$TEAMS $1" ;; esac; }
spctl_type_for() { case "$1" in *.app) echo exec ;; *.pkg) echo install ;; *) echo open ;; esac; }
spctl_assess() {  # path evidence -> sets SP_RESULT (accepted|rejected) SP_SOURCE
    local t; t="$(spctl_type_for "$1")"
    local out cmd
    if [ "$t" = open ]; then cmd=(spctl -a -vvv -t open --context context:primary-signature "$1")
    else cmd=(spctl -a -vvv -t "$t" "$1"); fi
    out="$("${cmd[@]}" 2>&1)"
    { printf '$'; printf ' %q' "${cmd[@]}"; printf '\n%s\n' "$out"; } >> "$2"
    case "$out" in *": accepted"*) SP_RESULT=accepted ;; *) SP_RESULT=rejected ;; esac
    SP_SOURCE="$(printf '%s\n' "$out" | sed -n 's/^source=//p' | head -1)"
    [ -n "$SP_SOURCE" ] || SP_SOURCE="$(printf '%s\n' "$out" | grep -v '^$' | tail -1 | sed "s|^.*: ||")"
    [ "$SP_SOURCE" = rejected ] && SP_SOURCE="rejected with no usable Developer ID signature (ad-hoc/unsigned)"
}
stapled() {  # path evidence -> 0 stapled, 1 not, 2 unknown (no CLT)
    [ "$HAVE_CLT" = 1 ] || return 2
    run_ev "$2" xcrun stapler validate -v "$1" && return 0
    return 1
}

sign_bundle() {  # path label container_given
    local b="$1" label="$2" contained="$3" e; e="$(ev "signing-$label")"
    if run_ev "$e" codesign --verify --deep --strict -vvv "$b"; then
        rec signing "$label codesign verify" PASS "valid on disk, satisfies its designated requirement" "$e"
    else
        rec signing "$label codesign verify" FAIL "codesign --verify --deep --strict failed" "$e"
    fi
    local d; d="$(codesign -dvvv "$b" 2>&1)"; printf '%s\n' "$d" >> "$e"
    run_ev "$e" codesign -d --entitlements - --xml "$b"
    local auth team flags ts
    auth="$(printf '%s\n' "$d" | sed -n 's/^Authority=//p' | head -1)"
    team="$(printf '%s\n' "$d" | sed -n 's/^TeamIdentifier=//p' | head -1)"
    flags="$(printf '%s\n' "$d" | sed -n 's/.*flags=[^(]*(\([^)]*\)).*/\1/p' | head -1)"
    ts="$(printf '%s\n' "$d" | sed -n 's/^Timestamp=//p' | head -1)"
    local devid=0
    case "$auth" in "Developer ID Application:"*) devid=1 ;; esac
    if [ "$devid" = 1 ] && [ -n "$team" ] && [ "$team" != "not set" ]; then
        note_team "$team"
        rec signing "$label identity" PASS "$auth; Team ID $team" "$e"
    else
        rec signing "$label identity" FAIL "not Developer ID signed (Authority: ${auth:-none}; flags: ${flags:-none}; Team ID: ${team:-none}). Expected until the owner signs" "$e"
    fi
    if [ "$devid" = 1 ]; then
        case ",$flags," in *runtime*) rec signing "$label hardened runtime" PASS "flags $flags" "$e" ;;
            *) rec signing "$label hardened runtime" FAIL "flags ${flags:-none}; notarization requires --options runtime" "$e" ;; esac
        if [ -n "$ts" ]; then rec signing "$label secure timestamp" PASS "$ts" "$e"
        else rec signing "$label secure timestamp" FAIL "no secure timestamp (sign with --timestamp)" "$e"; fi
        local nested nt bad=""
        while IFS= read -r nested; do
            [ "$nested" = "$(bundle_exec "$b")" ] && continue
            nt="$(codesign -dvv "$nested" 2>&1 | sed -n 's/^TeamIdentifier=//p')"
            printf 'nested %s TeamIdentifier=%s\n' "${nested#"$b"/}" "$nt" >> "$e"
            [ "$nt" = "$team" ] || bad="$bad ${nested##*/}($nt)"
        done < <(macho_files "$b")
        if [ -n "$bad" ]; then rec signing "$label nested code team" FAIL "different/no Team ID:$bad (library validation will refuse to load)" "$e"
        else rec signing "$label nested code team" PASS "every embedded Mach-O signed by $team" "$e"; fi
    fi
    spctl_assess "$b" "$e"
    if [ "$SP_RESULT" = accepted ]; then
        rec signing "$label Gatekeeper (spctl -t $(spctl_type_for "$b"))" PASS "accepted; source=$SP_SOURCE" "$e"
    else
        rec signing "$label Gatekeeper (spctl -t $(spctl_type_for "$b"))" FAIL "rejected: $SP_SOURCE" "$e"
    fi
    local st notarized=0
    case "$SP_SOURCE" in *Notarized*) notarized=1 ;; esac
    stapled "$b" "$e"; st=$?
    if [ "$st" = 0 ]; then rec signing "$label notarization ticket" PASS "stapled ticket validates" "$e"
    elif [ "$notarized" = 1 ]; then rec signing "$label notarization ticket" PASS "Gatekeeper reports Notarized Developer ID$([ "$st" = 2 ] && echo ' (stapler unavailable)' || echo '; no stapled ticket on the bundle (fine inside a stapled pkg/dmg)')" "$e"
    elif [ "$contained" = 1 ] && [ "$st" = 1 ]; then rec signing "$label notarization ticket" FAIL "not notarized per Gatekeeper and no stapled ticket (container ticket is checked separately)" "$e"
    else rec signing "$label notarization ticket" FAIL "not notarized: Gatekeeper source '${SP_SOURCE:-none}'$([ "$st" = 2 ] && echo ', stapler unavailable (no CLT)')" "$e"; fi
    selfcontained "$b" "$label" "$e"
    local ex ar; ex="$(bundle_exec "$b")"; ar="$(archs_of "$ex")"
    case "$ar" in *arm64*x86_64*|*x86_64*arm64*) rec signing "$label architectures" PASS "universal: $ar" "$e" ;;
        *) rec signing "$label architectures" FAIL "expected arm64 + x86_64, got '${ar:-unknown}'" "$e" ;; esac
}

selfcontained() {  # bundle label evidence: no build-machine load paths, embedded libs reachable
    local b="$1" label="$2" e="$3" ex abs="" haslp=0 needs=0
    ex="$(bundle_exec "$b")"; [ -f "$ex" ] || return 0
    if [ "$HAVE_CLT" = 1 ]; then
        run_ev "$e" otool -L "$ex"
        local rp; rp="$(otool -l "$ex" 2>/dev/null | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')"
        printf 'LC_RPATH:\n%s\n' "$rp" >> "$e"
        abs="$( { printf '%s\n' "$rp" | grep '^/'; otool -L "$ex" 2>/dev/null | awk '/^\t/{print $1}' | grep -E '^/(Users|opt/homebrew|usr/local)/'; } | sort -u | tr '\n' ' ')"
        printf '%s\n' "$rp" | grep -qx '@loader_path/../Frameworks' && haslp=1
        otool -L "$ex" 2>/dev/null | grep -q '@rpath/' && needs=1
    else   # no otool on a stock Mac: load commands are plain C strings in the binary
        abs="$(grep -a -o -E '/(Users|opt/homebrew|usr/local)/[[:print:]]+' "$ex" 2>/dev/null \
            | grep -vE '\.(c|cc|cpp|h|hpp|mm|m)$' | grep -iE '/lib$|/lib/|\.dylib|\.framework|/Frameworks' | sort -u | head -3 | tr '\n' ' ')"
        grep -a -q -F '@loader_path/../Frameworks' "$ex" && haslp=1
        grep -a -q -F '@rpath/' "$ex" && needs=1
        echo "otool unavailable; byte scan: abs='$abs' loader_path_rpath=$haslp uses_rpath=$needs" >> "$e"
    fi
    if [ -n "$abs" ]; then
        rec signing "$label self-contained load paths" FAIL "references build-machine paths: $abs" "$e"
    elif [ "$needs" = 1 ] && [ "$haslp" = 0 ] && [ -d "$b/Contents/Frameworks" ]; then
        rec signing "$label self-contained load paths" FAIL "links @rpath libraries but has no @loader_path/../Frameworks rpath: embedded Frameworks cannot load" "$e"
    else
        rec signing "$label self-contained load paths" PASS "no absolute build-machine paths$([ "$needs" = 1 ] && echo '; @rpath resolves to Contents/Frameworks')" "$e"
    fi
}

sign_container() {  # pkg or dmg or archive
    local c="$1" base; base="$(basename "$c")"
    local e; e="$(ev "signing-$base")"
    case "$c" in
        *.pkg)
            run_ev "$e" pkgutil --check-signature "$c"
            local out; out="$(pkgutil --check-signature "$c" 2>&1)"
            local cert; cert="$(printf '%s\n' "$out" | sed -n 's/^ *1\. //p' | head -1)"
            case "$out" in
                *"Developer ID Installer"*)
                    local t; t="$(printf '%s' "$cert" | sed -n 's/.*(\([A-Z0-9]\{10\}\)).*/\1/p')"; note_team "$t"
                    rec signing "$base installer signature" PASS "$cert; Team ID ${t:-?}" "$e" ;;
                *"no signature"*) rec signing "$base installer signature" FAIL "pkg is unsigned (productsign with Developer ID Installer). Expected until the owner signs" "$e" ;;
                *) rec signing "$base installer signature" FAIL "not signed by Developer ID Installer: ${cert:-unknown}" "$e" ;;
            esac
            case "$out" in *"Notarization: trusted by the Apple notary service"*)
                rec signing "$base pkgutil notarization" PASS "trusted by the Apple notary service" "$e" ;;
                *) rec signing "$base pkgutil notarization" FAIL "pkgutil reports no notarization" "$e" ;; esac ;;
        *.dmg)
            if run_ev "$e" codesign --verify --strict -vvv "$c"; then
                local dt; dt="$(codesign -dvv "$c" 2>&1 | sed -n 's/^TeamIdentifier=//p')"; note_team "$dt"
                rec signing "$base dmg signature" PASS "Team ID ${dt:-?}" "$e"
            else rec signing "$base dmg signature" FAIL "disk image is not code-signed" "$e"; fi ;;
        *)
            rec signing "$base archive" INFO "archives cannot carry a signature or ticket; each bundle must be notarized+stapled itself" "$e"
            return ;;
    esac
    spctl_assess "$c" "$e"
    if [ "$SP_RESULT" = accepted ]; then rec signing "$base Gatekeeper (spctl -t $(spctl_type_for "$c"))" PASS "accepted; source=$SP_SOURCE" "$e"
    else rec signing "$base Gatekeeper (spctl -t $(spctl_type_for "$c"))" FAIL "rejected: $SP_SOURCE" "$e"; fi
    local st; stapled "$c" "$e"; st=$?
    case "$st" in
        0) rec signing "$base stapled ticket" PASS "stapler validate OK" "$e" ;;
        1) rec signing "$base stapled ticket" FAIL "no stapled notarization ticket (xcrun stapler staple)" "$e" ;;
        2) rec signing "$base stapled ticket" SKIP "stapler needs Command Line Tools; see pkgutil/spctl result above" "$e" ;;
    esac
}

signing() {
    hdr "SIGNING"
    local c k contained=0
    [ "${#CONTAINERS[@]}" -gt 0 ] && contained=1
    for c in ${CONTAINERS[@]+"${CONTAINERS[@]}"}; do sign_container "$c"; done
    for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
        sign_bundle "${INV_PATH[$k]}" "$(prod_name "${INV_PROD[$k]}") ${INV_FMT[$k]}" "$contained"
    done
    local nteams; nteams="$(printf '%s' "$TEAMS" | wc -w | tr -d ' ')"
    if [ "$nteams" -eq 0 ]; then rec signing "Team ID" FAIL "no Developer ID Team ID on any artefact" ""
    elif [ "$nteams" -gt 1 ]; then rec signing "Team ID" FAIL "more than one Team ID:$TEAMS" ""
    else rec signing "Team ID" PASS "single Team ID:$TEAMS" ""; fi
    if [ -n "$EXPECT_TEAM" ]; then
        if [ "$(printf '%s' "$TEAMS" | tr -d ' ')" = "$EXPECT_TEAM" ]; then rec signing "expected Team ID" PASS "$EXPECT_TEAM" ""
        else rec signing "expected Team ID" FAIL "expected $EXPECT_TEAM, found:${TEAMS:- none}" ""; fi
    fi
}

# ============================================================================= QUARANTINE
quarantine() {
    hdr "QUARANTINE (simulated browser download)"
    local qdir="$RES/quarantine-copies" items=() c k
    for c in ${CONTAINERS[@]+"${CONTAINERS[@]}"}; do items+=("$c"); done
    if [ "${#items[@]}" -eq 0 ]; then
        for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do items+=("${INV_PATH[$k]}"); done
    fi
    local qval; qval="0083;$(printf '%x' "$(date +%s)");Safari;$(uuidgen)"
    local it q e
    for it in ${items[@]+"${items[@]}"}; do
        e="$(ev "quarantine-$(basename "$it")")"
        q="$qdir/$(basename "$it")"
        if [ "$DRY_RUN" = 1 ]; then
            act "$e" ditto "$it" "$q"
            act "$e" xattr -w com.apple.quarantine "$qval" "$q"
            rec quarantine "$(basename "$it")" SKIP "dry-run: would copy, quarantine and assess" "$e"
            continue
        fi
        mkdir -p "$qdir"
        act "$e" ditto "$it" "$q"
        act "$e" xattr -w com.apple.quarantine "$qval" "$q"
        run_ev "$e" xattr -p com.apple.quarantine "$q"
        spctl_assess "$q" "$e"
        if [ "$SP_RESULT" = accepted ]; then
            rec quarantine "$(basename "$it") quarantined assessment" PASS "Gatekeeper accepts the downloaded copy; source=$SP_SOURCE" "$e"
        else
            rec quarantine "$(basename "$it") quarantined assessment" FAIL "Gatekeeper would block the downloaded copy: $SP_SOURCE" "$e"
        fi
        if [ -x /usr/bin/syspolicy_check ] && [ -d "$q" ]; then
            if run_ev "$e" /usr/bin/syspolicy_check distribution "$q"; then
                rec quarantine "$(basename "$it") syspolicy_check distribution" PASS "no distribution issues reported" "$e"
            else
                rec quarantine "$(basename "$it") syspolicy_check distribution" FAIL "issues reported (see evidence)" "$e"
            fi
        fi
    done
    manual quarantine "In Finder, double-click each item in $(basename "$qdir")/ (a .pkg opens Installer; an .app launches). Record the exact Gatekeeper dialog text or 'no dialog'. A notarized item shows at most an 'downloaded from the Internet. Are you sure?' prompt; 'cannot be opened because Apple cannot check it' or 'damaged' = FAIL. Screenshot it. Delete the folder afterwards."
}

# ============================================================================= INSTALL
dest_for() {  # bundle -> destination directory by format + scope
    case "$(bundle_fmt "$1")" in
        AU)   [ "$SCOPE" = system ] && echo "$SYS_COMP" || echo "$USR_COMP" ;;
        VST3) [ "$SCOPE" = system ] && echo "$SYS_VST3" || echo "$USR_VST3" ;;
        AAX)  echo "$SYS_AAX" ;;
        Standalone) [ "$SCOPE" = system ] && echo "$SYS_APPS" || echo "$USR_APPS" ;;
    esac
}
expand_payload_into_root() {  # pkg evidence  (fake-root test path for pkgs)
    local pkg="$1" e="$2" x="$WORK/rootexp-$RANDOM" comp loc
    run_ev "$e" pkgutil --expand-full "$pkg" "$x" || return 1
    for comp in "$x" "$x"/*.pkg; do
        [ -d "$comp/Payload" ] || continue
        loc="$(sed -n 's/.*install-location="\([^"]*\)".*/\1/p' "$comp/PackageInfo" 2>/dev/null | head -1)"
        [ -n "$loc" ] || loc="/"
        act "$e" mkdir -p "$ROOT$loc"
        act "$e" ditto "$comp/Payload" "$ROOT$loc"
    done
}
std_dirs_state() { bundle_dirs | while IFS= read -r d; do [ -d "$d" ] && echo "$d"; done | sort; }

do_install() {  # phase manifest-file
    local phase="$1" man="$2" e; e="$(ev "$phase-actions")"
    local before="$WORK/$phase-before.txt" after="$WORK/$phase-after.txt"
    snapshot "$before"; std_dirs_state > "$WORK/$phase-stddirs-before.txt"
    local rbefore="$WORK/$phase-receipts-before.txt"; : > "$rbefore"
    [ "$REAL" = 1 ] && pkgutil --pkgs 2>/dev/null | sort > "$rbefore"
    local planned="" p k b dest
    if [ "${#PKG_TO_INSTALL[@]}" -gt 0 ]; then
        for p in "${PKG_TO_INSTALL[@]}"; do
            if [ "$REAL" = 1 ]; then
                need_sudo
                if act "$e" sudo installer -pkg "$p" -target / -verboseR; then
                    [ "$DRY_RUN" = 1 ] || rec "$phase" "installer $(basename "$p")" PASS "installer -target / succeeded" "$e"
                else
                    rec "$phase" "installer $(basename "$p")" FAIL "installer returned an error" "$e"
                fi
            else
                expand_payload_into_root "$p" "$e"
            fi
            planned="$planned$(pkgutil --payload-files "$p" 2>/dev/null | grep -iE '(^|/)sattari[^/]*$|\.(component|vst3|app|aaxplugin)$' | grep -vE '\.(component|vst3|app|aaxplugin)/' | sed "s|^\./|$ROOT/|")
"
        done
    else
        for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
            b="${INV_PATH[$k]}"; dest="$(dest_for "$b")"
            if [ "${INV_FMT[$k]}" = AAX ] && [ "$SCOPE" = user ]; then
                rec "$phase" "$(basename "$b")" SKIP "AAX installs system-wide only; re-run with --scope system to include it" "$e"; continue
            fi
            if [ -e "$dest/$(basename "$b")" ] || [ -L "$dest/$(basename "$b")" ]; then
                rec "$phase" "$(basename "$b")" FAIL "refusing to overwrite existing $dest/$(basename "$b") (not planned for install or removal)" "$e"; continue
            fi
            planned="$planned$dest/$(basename "$b")
"
            if [ "$SCOPE" = system ] && [ "$REAL" = 1 ]; then
                need_sudo; act "$e" sudo mkdir -p "$dest"; act "$e" sudo ditto "$b" "$dest/$(basename "$b")"
                act "$e" sudo chown -R root:wheel "$dest/$(basename "$b")"
            else
                act "$e" mkdir -p "$dest"; act "$e" ditto "$b" "$dest/$(basename "$b")"
            fi
            if [ -n "$(xattr -p com.apple.quarantine "$b" 2>/dev/null)" ]; then
                rec "$phase" "$(basename "$b") source quarantine" WARN "source bundle carries com.apple.quarantine (browser download); installed copy keeps it, as a tester's would" "$e"
            fi
        done
    fi
    if [ "$DRY_RUN" = 1 ]; then
        printf '# PLANNED (dry-run): paths the install would create\n%s' "$planned" | sed '/^$/d' > "${man%.tsv}.PLANNED.txt"
        rec "$phase" "manifest" SKIP "dry-run: planned paths in $(basename "${man%.tsv}").PLANNED.txt" "${man%.tsv}.PLANNED.txt"
        return 0
    fi
    hub_wait "$phase" "$e"
    snapshot "$after"
    write_manifest "$before" "$after" "$rbefore" "$man" "$phase"
}

hub_wait() {  # a suite pkg installs Sattari Hub.app; the Hub then installs the plug-ins
    local phase="$1" e="$2" p missing=""
    for p in $SEL_PRODUCTS; do
        find_installed "$p" AU >/dev/null || missing="$missing $(prod_name "$p")"
    done
    [ -z "$missing" ] && return 0
    local hub; hub="$(find "$SYS_APPS" "$USR_APPS" -maxdepth 1 -iname 'sattari hub*.app' 2>/dev/null | head -1)"
    [ -n "$hub" ] || return 0
    if [ "$NONINTERACTIVE" = 1 ] || [ ! -t 0 ]; then
        rec "$phase" "Hub-driven plug-in install" FAIL "pkg installed $(basename "$hub") but not:$missing (non-interactive)" "$e"; return 0
    fi
    echo "  The installer placed $(basename "$hub") but not:$missing"
    echo "  Open it, install$missing from the Hub (note every dialog), quit the Hub, then press Return here."
    manual "$phase" "Hub step: opened $(basename "$hub") and installed$missing. Record each dialog and click."
    read -r _
}

write_manifest() {  # before after receipts-before manifest phase
    local before="$1" after="$2" rbefore="$3" man="$4" phase="$5" e; e="$(ev "$phase-manifest")"
    printf '# kind\tpath\tbundle-id\tsha256-main-executable\n# written %s by %s\n' "$(date -u +%FT%TZ)" "$(basename "$SCRIPT_PATH")" > "$man"
    local new; new="$(comm -13 "$before" "$after")"
    local path top n=0
    # bundles are units; loose files are listed individually; new directories last (removed only if empty)
    printf '%s\n' "$new" | while IFS= read -r path; do
        [ -n "$path" ] || continue
        case "$path" in *.component/*|*.vst3/*|*.app/*|*.aaxplugin/*) continue ;; esac
        if [ -d "$path" ] && [ -f "$path/Contents/Info.plist" ]; then
            printf 'bundle\t%s\t%s\t%s\n' "$path" "$(bundle_id "$path")" "$(sha "$(bundle_exec "$path")")"
        elif [ -f "$path" ] || [ -L "$path" ]; then
            printf 'file\t%s\t-\t%s\n' "$path" "$(sha "$path")"
        fi
    done >> "$man"
    printf '%s\n' "$new" | while IFS= read -r path; do
        [ -n "$path" ] && [ -d "$path" ] && [ ! -f "$path/Contents/Info.plist" ] || continue
        case "$path" in *.component/*|*.vst3/*|*.app/*|*.aaxplugin/*) continue ;; esac
        printf 'dir\t%s\t-\t-\n' "$path"
    done | sort -r >> "$man"
    if [ "$REAL" = 1 ]; then
        pkgutil --pkgs 2>/dev/null | sort | comm -13 "$rbefore" - | grep -i sattari | while IFS= read -r top; do
            printf 'receipt\t%s\t-\t-\n' "$top"
            { echo "# pkgutil --files $top"; pkgutil --files "$top"; } >> "$e" 2>&1
        done >> "$man"
    fi
    comm -13 "$WORK/$phase-stddirs-before.txt" <(std_dirs_state) | while IFS= read -r top; do
        printf 'stddir\t%s\t-\t-\n' "$top"   # created standard folder: recorded, never removed
    done >> "$man"
    n="$(grep -c '^bundle' "$man")"
    rec "$phase" "manifest" INFO "$n bundle(s), $(grep -c '^file' "$man") file(s), $(grep -c '^dir' "$man") dir(s), $(grep -c '^receipt' "$man") receipt(s) created -> $(basename "$man")" "$man"
    cp "$man" "$e"
}

find_installed() {  # product format -> prints installed path, rc 1 if none
    local n ext d; n="$(prod_name "$1")"
    case "$2" in AU) ext=component ;; VST3) ext=vst3 ;; AAX) ext=aaxplugin ;; Standalone) ext=app ;; esac
    while IFS= read -r d; do
        [ -d "$d/$n.$ext" ] && { printf '%s\n' "$d/$n.$ext"; return 0; }
    done < <(printf '%s\n' "$USR_COMP" "$SYS_COMP" "$USR_VST3" "$SYS_VST3" "$SYS_AAX" "$USR_APPS" "$SYS_APPS")
    return 1
}

verify_installed() {  # phase manifest
    local phase="$1" man="$2" p fmt b e fp="$RES/fingerprints-$1.tsv"
    [ "$DRY_RUN" = 1 ] && { rec "$phase" "installed files" SKIP "dry-run" ""; return 0; }
    printf '# relpath\tsha256\tsha256-signature-removed\n' > "$fp"
    for p in $SEL_PRODUCTS; do
        for fmt in $(prod_formats "$p"); do
            e="$(ev "$phase-verify-$p-$fmt")"
            if ! b="$(find_installed "$p" "$fmt")"; then
                rec "$phase" "$(prod_name "$p") $fmt installed" FAIL "no $(prod_name "$p") $fmt bundle in any standard location" "$e"; continue
            fi
            grep -qF "$(printf '\t%s\t' "$b")" "$man" 2>/dev/null \
                && rec "$phase" "$(prod_name "$p") $fmt installed" PASS "$b" "$e" \
                || rec "$phase" "$(prod_name "$p") $fmt installed" FAIL "$b exists but was not created by this install" "$e"
            run_ev "$e" ls -laR "$b"
            run_ev "$e" codesign --verify --deep --strict -vvv "$b" \
                && rec "$phase" "$(prod_name "$p") $fmt signature intact" PASS "codesign verify after install" "$e" \
                || rec "$phase" "$(prod_name "$p") $fmt signature intact" FAIL "installed copy fails codesign verify" "$e"
            fingerprint_bundle "$b" | tee -a "$e" >> "$fp"
            local src="" k
            for k in ${INV_PATH[@]+"${!INV_PATH[@]}"}; do
                [ "${INV_PROD[$k]}" = "$p" ] && [ "${INV_FMT[$k]}" = "$fmt" ] && src="${INV_PATH[$k]}"
            done
            if [ -n "$src" ]; then
                if [ "$(fingerprint_bundle "$src" | cut -f2 | tr '\n' ' ')" = "$(fingerprint_bundle "$b" | cut -f2 | tr '\n' ' ')" ]; then
                    rec "$phase" "$(prod_name "$p") $fmt sha256 == artefact" PASS "$(sha "$(bundle_exec "$b")" | cut -c1-16) (main executable)" "$e"
                else
                    rec "$phase" "$(prod_name "$p") $fmt sha256 == artefact" FAIL "installed binaries differ from the artefact that was inspected" "$e"
                fi
            fi
            # readability: system-wide installs must be readable by every user
            local unread
            if case "$b" in "$HOME_R"/*) true ;; *) false ;; esac; then
                unread="$(find "$b" ! -perm -u+r 2>/dev/null | head -3)"
            else
                unread="$(find "$b" \( -type f ! -perm -o+r \) -o \( -type d ! -perm -o+rx \) 2>/dev/null | head -3)"
            fi
            [ -z "$unread" ] && rec "$phase" "$(prod_name "$p") $fmt permissions" PASS "owner $(stat -f %Su:%Sg "$b"), mode $(stat -f %Sp "$b")" "$e" \
                || rec "$phase" "$(prod_name "$p") $fmt permissions" FAIL "not readable by all users: $(printf '%s' "$unread" | tr '\n' ' ')" "$e"
            # embedded runtime / model-free contract
            local onnx="$b/Contents/Frameworks/libonnxruntime.1.22.0.dylib"
            case "$(prod_onnx "$p")" in
                required) [ -f "$onnx" ] && rec "$phase" "$(prod_name "$p") $fmt embedded ONNX Runtime" PASS "Frameworks/libonnxruntime.1.22.0.dylib $(archs_of "$onnx")" "$e" \
                          || rec "$phase" "$(prod_name "$p") $fmt embedded ONNX Runtime" FAIL "missing $onnx" "$e" ;;
                forbidden) [ -z "$(find "$b" -name 'libonnxruntime*' 2>/dev/null)" ] && rec "$phase" "$(prod_name "$p") $fmt model-free" PASS "no ONNX Runtime in bundle (by design)" "$e" \
                          || rec "$phase" "$(prod_name "$p") $fmt model-free" FAIL "Sattari Key must not embed ONNX Runtime" "$e" ;;
            esac
            # factory presets are compiled into the binary
            local fac; fac="$(prod_factory "$p")"
            if [ "$fmt" != Standalone ]; then
                if [ -z "$fac" ]; then
                    rec "$phase" "$(prod_name "$p") $fmt factory presets" SKIP "none defined for this product (no addFactoryPreset in source)" "$e"
                elif grep -a -q -F "$fac" "$(bundle_exec "$b")"; then
                    rec "$phase" "$(prod_name "$p") $fmt factory presets" PASS "embedded preset table present ('$fac' found in binary); confirm the menu in the DAW" "$e"
                else
                    rec "$phase" "$(prod_name "$p") $fmt factory presets" FAIL "factory preset name '$fac' not found in binary" "$e"
                fi
            fi
        done
    done
    compare_fingerprints "$fp" "$phase"
    local s; s="$(ev "$phase-support-files")"
    { echo "# Sattari support folders after install"; support_dirs | while IFS= read -r d; do
        if [ -e "$d" ]; then find "$d" -maxdepth 3; else echo "absent: $d"; fi; done; } >> "$s"
    if [ -d "$SUP_USER_LIB/models" ]; then
        rec "$phase" "optional key models" INFO "$SUP_USER_LIB/models present: $(find "$SUP_USER_LIB/models" -mindepth 1 -maxdepth 1 -exec basename {} \; | tr '\n' ' ')" "$s"
    else
        rec "$phase" "optional key models" INFO "none in $SUP_USER_LIB/models (expected: CREPE is embedded; neural key falls back)" "$s"
    fi
    rec "$phase" "user preset folder" INFO "created on first save, not by the installer; checklist step records the actual path" "$s"
    if grep -q 'Sattari Hub' "$man"; then
        local legal; legal="$(find "$SYS_APPS" "$USR_APPS" -maxdepth 4 -path '*Sattari Hub.app/Contents/Resources/Legal' 2>/dev/null | head -1)"
        [ -n "$legal" ] && rec "$phase" "licence notices" PASS "$(find "$legal" -mindepth 1 -maxdepth 1 -exec basename {} \; | tr '\n' ' ')" "$s" \
                        || rec "$phase" "licence notices" FAIL "Hub installed without Contents/Resources/Legal" "$s"
    fi
}

# ============================================================================= DISCOVERY
refresh_au_registry() {
    [ "$REAL" = 1 ] || return 0
    local e="$1"
    act "$e" killall -9 AudioComponentRegistrar
    [ "$DRY_RUN" = 1 ] || sleep 2
}
discovery() {  # phase
    local phase="$1" p b plist t s m e
    hdr "DISCOVERY ($phase)"
    if [ "$REAL" = 0 ]; then
        rec "$phase" "auval" SKIP "fake root: the AU registrar only scans real plug-in folders" ""
    elif [ "$DRY_RUN" = 1 ]; then
        e="$(ev "$phase-auval")"; refresh_au_registry "$e"
        for p in $SEL_PRODUCTS; do act "$e" auval -v "<type from Info.plist>" "$(prod_subtype "$p")" "$MANU"; done
        rec "$phase" "auval" SKIP "dry-run" "$e"
    else
        e="$(ev "$phase-auval-a")"; refresh_au_registry "$e"
        auval_list "$e" || rec "$phase" "auval -a" FAIL "$AUVAL_HANG" "$e"
        for p in $SEL_PRODUCTS; do
            b="$(find_installed "$p" AU)" || { rec "$phase" "$(prod_name "$p") AU listed" FAIL "no installed AU" ""; continue; }
            plist="$b/Contents/Info.plist"
            t="$(pl AudioComponents.0.type "$plist")"; s="$(pl AudioComponents.0.subtype "$plist")"; m="$(pl AudioComponents.0.manufacturer "$plist")"
            [ "$s" = "$(prod_subtype "$p")" ] && [ "$m" = "$MANU" ] \
                || rec "$phase" "$(prod_name "$p") AU identity" FAIL "Info.plist says $t $s $m; expected subtype $(prod_subtype "$p") manufacturer $MANU" ""
            if grep -qE "^${t}[[:space:]]+${s}[[:space:]]+${m}" "$e"; then
                rec "$phase" "$(prod_name "$p") AU listed" PASS "auval -a: $(grep -E "^${t}[[:space:]]+${s}[[:space:]]+${m}" "$e" | head -1)" "$e"
            else
                rec "$phase" "$(prod_name "$p") AU listed" FAIL "$t $s $m not in auval -a" "$e"
            fi
            local v; v="$(ev "$phase-auval-v-$s")"
            run_timeout "$AUVAL_TIMEOUT" "$v" auval -v "$t" "$s" "$m"
            if grep -q "AU VALIDATION SUCCEEDED" "$v"; then
                rec "$phase" "$(prod_name "$p") auval -v $t $s $m" PASS "AU VALIDATION SUCCEEDED" "$v"
            else
                rec "$phase" "$(prod_name "$p") auval -v $t $s $m" FAIL "$(grep -E 'FAIL|ERROR|timeout' "$v" | head -2 | tr '\n' ' ')" "$v"
            fi
        done
    fi
    if [ "$DRY_RUN" = 1 ]; then
        rec "$phase" "VST3 sanity + pluginval" SKIP "dry-run: nothing was installed by this run"
        [ "$phase" = discovery ] && daws
        return 0
    fi
    for p in $SEL_PRODUCTS; do
        b="$(find_installed "$p" VST3)" || { rec "$phase" "$(prod_name "$p") VST3 sanity" FAIL "no installed VST3" ""; continue; }
        e="$(ev "$phase-vst3-$p")"
        local mi="$b/Contents/Resources/moduleinfo.json" ver bad=""
        run_ev "$e" plutil -p "$b/Contents/Info.plist"; run_ev "$e" cat "$mi"
        ver="$(pl CFBundleShortVersionString "$b/Contents/Info.plist")"
        [ "$(pl CFBundlePackageType "$b/Contents/Info.plist")" = BNDL ] || bad="$bad CFBundlePackageType!=BNDL;"
        [ -x "$(bundle_exec "$b")" ] || bad="$bad executable missing;"
        [ -f "$mi" ] || bad="$bad no moduleinfo.json;"
        if [ -f "$mi" ]; then
            grep -q "\"Name\": \"$(prod_name "$p")\"" "$mi" || bad="$bad moduleinfo Name;"
            grep -q '"Vendor": "Sattari"' "$mi" || bad="$bad moduleinfo Vendor;"
            grep -q '"Audio Module Class"' "$mi" || bad="$bad no Audio Module Class;"
            grep -q "\"Version\": \"$ver\"" "$mi" || bad="$bad moduleinfo Version != $ver;"
        fi
        [ -z "$bad" ] && rec "$phase" "$(prod_name "$p") VST3 sanity" PASS "v$ver, BNDL, moduleinfo Name/Vendor/Class/Version consistent" "$e" \
                      || rec "$phase" "$(prod_name "$p") VST3 sanity" FAIL "$bad" "$e"
    done
    local pv=""
    [ -x /Applications/pluginval.app/Contents/MacOS/pluginval ] && pv=/Applications/pluginval.app/Contents/MacOS/pluginval
    [ -z "$pv" ] && command -v pluginval >/dev/null 2>&1 && pv="$(command -v pluginval)"
    if [ -z "$pv" ]; then rec "$phase" "pluginval" SKIP "not installed on this machine (optional)" ""
    elif [ "$NO_PLUGINVAL" = 1 ] || [ "$REAL" = 0 ]; then rec "$phase" "pluginval" SKIP "disabled ($([ "$REAL" = 0 ] && echo 'fake root' || echo '--no-pluginval'))" ""
    else
        for p in $SEL_PRODUCTS; do
            b="$(find_installed "$p" VST3)" || continue
            e="$(ev "$phase-pluginval-$p")"
            if [ "$DRY_RUN" = 1 ]; then act "$e" "$pv" --validate-in-process --strictness-level 5 "$b"; continue; fi
            if run_timeout "$PLUGINVAL_TIMEOUT" "$e" "$pv" --validate-in-process --strictness-level 5 "$b"; then
                rec "$phase" "$(prod_name "$p") pluginval strictness 5 (VST3)" PASS "exit 0" "$e"
            else
                rec "$phase" "$(prod_name "$p") pluginval strictness 5 (VST3)" FAIL "$(grep -E 'FAILED|\*\*\*' "$e" | head -2 | tr '\n' ' ')" "$e"
            fi
        done
    fi
    [ "$phase" = discovery ] && daws
}

daws() {
    local e; e="$(ev daws-installed)"
    local app ver found=0
    while IFS= read -r app; do
        [ -d "$app" ] || continue
        found=1; ver="$(pl CFBundleShortVersionString "$app/Contents/Info.plist")"
        echo "$app $ver" >> "$e"
        rec discovery "DAW installed" INFO "$(basename "$app" .app) $ver" "$e"
        case "$(basename "$app")" in
            Logic*) manual discovery "Logic Pro $ver: Settings > Plug-in Manager > select Sattari > Reset & Rescan Selection; both show 'successfully validated'. Insert on an audio track: Audio FX > Audio Units > Sattari > Sattari Auto Pitch and Sattari Key. Then run $DAW_CHECKLIST (Logic section)." ;;
            GarageBand*) manual discovery "GarageBand $ver: Plug-ins > Audio Units > Sattari lists both (optional host)." ;;
            Ableton*) manual discovery "$(basename "$app" .app) $ver: Settings > Plug-ins: 'Use Audio Units' ON and 'Use VST3 Plug-in System Folders' ON, click Rescan. Browser > Plug-ins shows Sattari under AU and VST3. Then run $DAW_CHECKLIST (Ableton section)." ;;
            FL*) manual discovery "$(basename "$app" .app) $ver: Options > Manage plugins > Find installed plugins (tick 'Verify plugins'); Sattari Auto Pitch and Sattari Key appear as VST3 (and AU). Then run $DAW_CHECKLIST (FL section)." ;;
            *) manual discovery "$(basename "$app" .app) $ver: rescan plug-ins; record whether Sattari Auto Pitch and Sattari Key appear (optional host)." ;;
        esac
    done < <(find /Applications "$HOME/Applications" -maxdepth 2 -type d \( -name 'Logic Pro*.app' -o -name 'GarageBand.app' -o -name 'Ableton Live*.app' \
             -o -name 'FL Studio*.app' -o -name 'REAPER*.app' -o -name 'Cubase*.app' -o -name 'Studio One*.app' -o -name 'Bitwig Studio*.app' -o -name 'Pro Tools*.app' \) 2>/dev/null | sort)
    [ "$found" = 1 ] || rec discovery "DAW installed" WARN "no Logic / Ableton / FL found; install the host matrix before the DAW checklist" "$e"
    local sa
    for p in $SEL_PRODUCTS; do
        sa="$(prod_name "$p").app"
        if [ "$DRY_RUN" = 1 ] || find_installed "$p" Standalone >/dev/null; then
            manual discovery "First launch of $sa from Finder (not Terminal): record any Gatekeeper dialog and the microphone-permission prompt; choose an audio device, confirm the UI opens, quit, relaunch (no dialog the second time)."
        fi
    done
}

# ============================================================================= UNINSTALL
allowed_parent() {  # path -> 0 if its parent is a standard install folder or inside a Sattari support folder
    local par; par="$(dirname "$1")" ; local d
    while IFS= read -r d; do [ "$par" = "$d" ] && return 0; done < <(bundle_dirs)
    while IFS= read -r d; do case "$1" in "$d"|"$d"/*) return 0 ;; esac; done < <(support_dirs)
    while IFS= read -r d; do case "$1" in "$d"/*) is_sattari_name "$(dirname "$1")" || is_sattari_name "$1" && return 0 ;; esac; done < <(preset_dirs)
    while IFS= read -r d; do [ "$par" = "$d" ] && return 0; done < <(pref_dirs)
    return 1
}
safe_to_remove() {  # kind path id sha planned -> 0 ok; else prints reason
    local kind="$1" path="$2" id="$3" want="$4" planned="$5"
    case "$path" in /*) ;; *) echo "not absolute"; return 1 ;; esac
    case "$path" in *"/../"*|*"/./"*|*/..|*/.) echo "path traversal"; return 1 ;; esac
    if [ -n "$ROOT" ]; then case "$path" in "$ROOT"/*) ;; *) echo "outside fake root $ROOT"; return 1 ;; esac; fi
    allowed_parent "$path" || { echo "not in a standard install/support location"; return 1; }
    case "$path" in *"/Sattari Audio"|*"/Sattari Audio/"*) ;; *) is_sattari_name "$path" || { echo "name is not Sattari*"; return 1; } ;; esac
    [ "$planned" = 1 ] && return 0
    [ -L "$path" ] && { echo "is a symlink"; return 1; }
    if [ "$kind" = bundle ]; then
        local cur; cur="$(bundle_id "$path")"
        case "$(lower "$cur")" in com.sattari.*) ;; *) echo "bundle id '$cur' is not com.sattari.*"; return 1 ;; esac
        [ "$cur" = "$id" ] || { echo "bundle id changed ($id -> $cur)"; return 1; }
        [ "$(sha "$(bundle_exec "$path")")" = "$want" ] || { echo "executable changed since install"; return 1; }
    elif [ "$kind" = file ] && [ "$want" != "-" ]; then
        [ "$(sha "$path")" = "$want" ] || { echo "file changed since install (user data?)"; return 1; }
    fi
    return 0
}

uninstall() {  # phase manifest [planned-file]
    local phase="$1" man="$2" e; e="$(ev "$phase-actions")"
    hdr "UNINSTALL ($phase)"
    local planned=0 kind path id want why refused=0 removed=0
    if [ "$DRY_RUN" = 1 ] && [ ! -s "$man" ]; then
        planned=1; man="${man%.tsv}.PLANNED.txt"
        [ -f "$man" ] || { rec "$phase" "manifest" SKIP "nothing planned" ""; return 0; }
    fi
    [ -f "$man" ] || { rec "$phase" "manifest" FAIL "no manifest at $man: refusing to remove anything" ""; return 0; }
    run_ev "$e" cat "$man"
    while IFS="$(printf '\t')" read -r kind path id want; do
        case "$kind" in '#'*|'') continue ;; esac
        if [ "$planned" = 1 ]; then path="$kind"; kind=bundle; id=-; want=-; fi
        case "$kind" in
            bundle|file)
                if ! why="$(safe_to_remove "$kind" "$path" "$id" "$want" "$planned")"; then
                    refused=$((refused + 1)); rec "$phase" "refuse $(basename "$path")" FAIL "$why; left in place" "$e"; continue
                fi
                if [ "$planned" = 0 ] && [ ! -e "$path" ] && [ ! -L "$path" ]; then echo "already gone: $path" >> "$e"; continue; fi
                if [ "$REAL" = 1 ] && [ ! -w "$(dirname "$path")" ]; then need_sudo; act "$e" sudo rm -rf -- "$path"
                else act "$e" rm -rf -- "$path"; fi
                removed=$((removed + 1)) ;;
            dir)
                if ! why="$(safe_to_remove dir "$path" - - 1)"; then rec "$phase" "refuse $path" FAIL "$why" "$e"; continue; fi
                if [ "$REAL" = 1 ] && [ -d "$path" ] && [ ! -w "$(dirname "$path")" ]; then act "$e" sudo rmdir "$path"
                else act "$e" rmdir "$path"; fi ;;
            receipt)
                case "$(lower "$path")" in *sattari*) need_sudo; act "$e" sudo pkgutil --forget "$path" ;;
                    *) rec "$phase" "refuse receipt $path" FAIL "not a Sattari receipt" "$e" ;; esac ;;
            stddir) echo "kept standard folder created by install: $path" >> "$e" ;;
        esac
    done < "$man"
    if [ "$DRY_RUN" = 1 ]; then rec "$phase" "remove" SKIP "dry-run: $removed path(s) would be removed, $refused refused" "$e"; return 0; fi
    rec "$phase" "removed" "$([ "$refused" = 0 ] && echo PASS || echo FAIL)" "$removed manifest path(s) removed, $refused refused" "$e"
    # verify
    local left=""
    while IFS="$(printf '\t')" read -r kind path id want; do
        case "$kind" in bundle|file) { [ -e "$path" ] || [ -L "$path" ]; } && left="$left $path" ;; esac
    done < "$man"
    [ -z "$left" ] && rec "$phase" "installed files gone" PASS "every bundle/file in $(basename "$man") is gone" "$e" \
                   || rec "$phase" "installed files gone" FAIL "still present:$left" "$e"
    local residue r; r="$(ev "$phase-residue")"; residue="$(sattari_scan | sort -u)"
    printf '%s\n' "${residue:-<none>}" >> "$r"
    if [ -z "$residue" ]; then rec "$phase" "no Sattari residue" PASS "full preflight scan finds nothing" "$r"
    else rec "$phase" "no Sattari residue" WARN "Sattari paths remain that this uninstall did not remove (runtime data such as the cross-plugin bus or saved presets, or entries refused above): $(printf '%s' "$residue" | head -4 | tr '\n' ';')" "$r"; fi
    if [ "$REAL" = 1 ]; then
        local rr; rr="$(pkgutil --pkgs 2>/dev/null | grep -i sattari)"
        [ -z "$rr" ] && rec "$phase" "receipts forgotten" PASS "no Sattari receipts" "$e" || rec "$phase" "receipts forgotten" FAIL "$rr" "$e"
        local a; a="$(ev "$phase-auval-a")"; refresh_au_registry "$a"
        if ! auval_list "$a"; then rec "$phase" "AU registry clean" FAIL "$AUVAL_HANG" "$a"
        elif grep -qE "[[:space:]]${MANU}[[:space:]]" "$a"; then rec "$phase" "AU registry clean" FAIL "auval -a still lists $MANU" "$a"
        else rec "$phase" "AU registry clean" PASS "auval -a lists no $MANU components" "$a"; fi
    fi
}

# ============================================================================= SUMMARY
summary() {
    local s="$RES/summary.md" verdict
    local nf; nf="$(awk -F'\t' '$3=="FAIL"' "$RES/results.tsv" | wc -l | tr -d ' ')"
    if [ "${PREFLIGHT_ABORT:-0}" = 1 ]; then verdict="STOPPED - machine not clean (nothing changed)"
    elif [ "$nf" -eq 0 ]; then verdict="PASS (automated checks) - manual steps below still required"
    else verdict="FAIL - $nf failing check(s)"; fi
    {
        echo "# Sattari clean-machine qualification - $STAMP"
        echo
        echo "| field | value |"
        echo "|---|---|"
        echo "| verdict | **$verdict** |"
        echo "| candidate | ${CANDIDATE:-not given (--candidate)} |"
        echo "| products | $SEL_PRODUCTS |"
        echo "| mode | $([ "$DRY_RUN" = 1 ] && echo DRY-RUN || echo live)$([ "$REAL" = 0 ] && echo ", FAKE ROOT $ROOT"), scope $SCOPE |"
        echo "| machine | macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion)), $(uname -m), $(sysctl -n hw.model), VM=$(sysctl -n kern.hv_vmm_present 2>/dev/null) |"
        echo "| script | $(basename "$SCRIPT_PATH") sha256 $(sha "$SCRIPT_PATH" | cut -c1-16) |"
        for f in ${PKGS[@]+"${PKGS[@]}"} ${DMGS[@]+"${DMGS[@]}"} ${BSRCS[@]+"${BSRCS[@]}"} ${UNINSTALL_FROM:+"$UNINSTALL_FROM"}; do
            if [ -f "$f" ]; then echo "| input | \`$(basename "$f")\` sha256 $(sha "$f") |"; else echo "| input | \`$f\` (folder) |"; fi
        done
        echo
        echo "## Phases"
        echo
        echo "| phase | status | pass | fail | skip | warn | manual |"
        echo "|---|---|---|---|---|---|---|"
        awk -F'\t' '
            { if (!($1 in seen)) { seen[$1]=1; order[++n]=$1 } c[$1","$3]++ }
            END { for (i=1;i<=n;i++) { ph=order[i]; st="PASS"
                    if (c[ph",FAIL"]>0) st="**FAIL**"
                    else if (c[ph",PASS"]==0 && c[ph",SKIP"]>0) st="SKIP"
                    else if (c[ph",PASS"]==0) st="INFO"
                    printf "| %s | %s | %d | %d | %d | %d | %d |\n", ph, st, c[ph",PASS"], c[ph",FAIL"], c[ph",SKIP"], c[ph",WARN"], c[ph",MANUAL"] } }' "$RES/results.tsv"
        echo
        echo "## Checks"
        echo
        echo "| phase | check | status | detail | evidence |"
        echo "|---|---|---|---|---|"
        awk -F'\t' '{ gsub(/\|/,"/",$4); ev=($5=="")?"":"`"$5"`"; st=($3=="FAIL")?"**FAIL**":$3; printf "| %s | %s | %s | %s | %s |\n", $1, $2, st, $4, ev }' "$RES/results.tsv"
        echo
        echo "## Manual steps (record PASS/FAIL/NOTES in $CM_CHECKLIST)"
        echo
        if [ -s "$RES/manual-steps.md" ]; then cat "$RES/manual-steps.md"; else echo "- none generated"; fi
        echo
        echo "## Files"
        echo
        echo "- \`results.tsv\` every check; \`evidence/\` raw command output; \`actions.log\` every change made (or planned)"
        echo "- \`fingerprints-*.tsv\` sha256 and signature-removed sha256 of every Mach-O (source, install, reinstall)"
        echo "- \`manifest-*.tsv\` exactly what each install created; remove later with"
        echo "  \`$(basename "$SCRIPT_PATH") --uninstall-from <results>/manifest-reinstall.tsv\`"
    } > "$s"
    echo
    echo "== $verdict =="
    echo "results: $RES"
    echo "summary: $s"
}

# ============================================================================= MAIN
PREFLIGHT_ABORT=0
echo "Sattari clean-machine qualification  $STAMP  $([ "$DRY_RUN" = 1 ] && echo '[DRY-RUN]') $([ "$REAL" = 0 ] && echo "[FAKE ROOT $ROOT]")"
echo "results folder: $RES"

if [ -n "$UNINSTALL_FROM" ]; then
    cp "$UNINSTALL_FROM" "$RES/manifest-input.tsv"
    uninstall uninstall "$RES/manifest-input.tsv"
    summary
    awk -F'\t' '$3=="FAIL"{f=1} END{exit f}' "$RES/results.tsv"; exit $?
fi

if [ "$FINGERPRINT_ONLY" = 1 ]; then
    inventory
    cp "$RES/fingerprints-source.tsv" "$RES/fingerprints.tsv"
    summary
    exit 0
fi

preflight
if [ "$PREFLIGHT_ABORT" = 1 ]; then summary; exit 3; fi
inventory
signing
quarantine
hdr "INSTALL"
do_install install "$RES/manifest-install.tsv"
verify_installed install "$RES/manifest-install.tsv"
discovery discovery
uninstall uninstall "$RES/manifest-install.tsv"
if [ "$NO_REINSTALL" = 0 ]; then
    hdr "REINSTALL"
    do_install reinstall "$RES/manifest-reinstall.tsv"
    verify_installed reinstall "$RES/manifest-reinstall.tsv"
    discovery reinstall
    if [ "$FINAL_UNINSTALL" = 1 ]; then
        uninstall final-uninstall "$RES/manifest-reinstall.tsv"
    else
        rec reinstall "left installed" INFO "for the DAW checklist; remove later with --uninstall-from $RES/manifest-reinstall.tsv" ""
    fi
fi
summary
awk -F'\t' '$3=="FAIL"{f=1} END{exit f}' "$RES/results.tsv"
