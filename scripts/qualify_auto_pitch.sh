#!/usr/bin/env bash
# =============================================================================
#  Sattari Auto Pitch - release qualification from FRESHLY BUILT binaries
# -----------------------------------------------------------------------------
#  Rebuilds every binary it runs (tool targets are EXCLUDE_FROM_ALL, so a plain
#  `cmake --build` leaves them stale - that happened on 2026-09-25), proves the
#  build is up to date with a ninja dry run, records SHA-256 fingerprints of each
#  binary and of the installed plugins, then runs the machine gates and writes one
#  dated report: validation/auto-pitch/qualification/<date>-<sha>.md
#
#  Machine gates only. Listening, DAW matrix, signing and licensing are recorded
#  in the release-readiness file, not proven here.
#
#  Usage: scripts/qualify_auto_pitch.sh [build dir]   (default build/auto-pitch-neural)
#  Env:   MIN_FREE_GB (default 8)   EXPECTED_LATENCY (default 2885)   LOAD_CYCLES (default 30)
#         NOHARM_MINUTES (default 120: AutoPitchNoHarm playback soak, ~15 min wall clock; 0 = checks 1-7 only; -1 = skip, run separately)
#         PYTHON (default ~/.venvs/sattari-audio/bin/python if present, else python3)
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
B="${1:-build/auto-pitch-neural}"
MIN_FREE_GB="${MIN_FREE_GB:-8}"; EXPECTED_LATENCY="${EXPECTED_LATENCY:-2885}"; LOAD_CYCLES="${LOAD_CYCLES:-30}"
NOHARM_MINUTES="${NOHARM_MINUTES:-120}"
PY="${PYTHON:-}"; [ -z "$PY" ] && { [ -x "$HOME/.venvs/sattari-audio/bin/python" ] && PY="$HOME/.venvs/sattari-audio/bin/python" || PY=python3; }
TOOLS=(AutoPitchCoreContract AutoPitchRender AutoPitchObjectiveBench AutoPitchBench AutoPitchNoHarm)
VST3="$HOME/Library/Audio/Plug-Ins/VST3/Sattari Auto Pitch.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/Sattari Auto Pitch.component"
SHA="$(git rev-parse --short HEAD)"; DATE="$(date +%Y-%m-%d)"
OUTDIR="validation/auto-pitch/qualification"; mkdir -p "$OUTDIR"; REPORT="$OUTDIR/$DATE-$SHA.md"
LOG="$(mktemp -d /tmp/qualify-auto-pitch.XXXXXX)"
fail=0; rows=()
gate() {  # gate <name> <command...>   (PASS on exit 0)
  local name="$1"; shift; local t0=$SECONDS; local f="$LOG/$(echo "$name" | tr -c 'A-Za-z0-9' _).log"
  if "$@" >"$f" 2>&1; then rows+=("| $name | PASS | $((SECONDS-t0)) s |"); echo "PASS  $name"
  else rows+=("| $name | **FAIL** (log $f) | $((SECONDS-t0)) s |"); echo "FAIL  $name  -> $f"; fail=1; fi
}

# --- 0. preflight --------------------------------------------------------------
free_gb=$(df -g / | awk 'NR==2 {print $4}')
if [ "$free_gb" -lt "$MIN_FREE_GB" ]; then echo "ABORT: ${free_gb} GB free < ${MIN_FREE_GB} GB"; exit 2; fi
dirty="$(git status --porcelain -- plugins core cmake CMakeLists.txt third_party | grep -v '^??' || true)"

# --- 1. fresh build + freshness proof ------------------------------------------
echo "== building (plugin + ${TOOLS[*]})"
cmake --build "$B" -j"$(sysctl -n hw.ncpu)" >"$LOG/build_plugin.log" 2>&1 || { echo "ABORT: plugin build failed ($LOG/build_plugin.log)"; exit 2; }
cmake --build "$B" --target "${TOOLS[@]}" -j"$(sysctl -n hw.ncpu)" >"$LOG/build_tools.log" 2>&1 || { echo "ABORT: tool build failed ($LOG/build_tools.log)"; exit 2; }
# Freshness proof: a second, REAL build of the same targets must do nothing. (A ninja dry run
# cannot prove this: with CONFIGURE_DEPENDS globs it stops at the CMake re-check and never
# looks at the targets, so it reports nothing even when a binary is stale.)
second="$(cmake --build "$B" --target all "${TOOLS[@]}" 2>&1)"
echo "$second" | grep -q 'ninja: no work to do' || { echo "ABORT: build not up to date after building:"; echo "$second" | tail -5; exit 2; }
bin() { find "$B" -type f -name "$1" -perm +111 -not -path '*.dSYM*' | head -n1; }
fp=(); for t in "${TOOLS[@]}"; do p="$(bin "$t")"; [ -z "$p" ] && { echo "ABORT: $t not built"; exit 2; }; fp+=("| $t | \`$(shasum -a 256 "$p" | cut -c1-16)\` | $(stat -f %Sm -t '%Y-%m-%d %H:%M:%S' "$p") |"); done
built_vst3="$(find "$B" -path '*Release/VST3/Sattari Auto Pitch.vst3/Contents/MacOS/Sattari Auto Pitch' | head -n1)"
inst_vst3="$VST3/Contents/MacOS/Sattari Auto Pitch"
if [ -f "$built_vst3" ] && [ -f "$inst_vst3" ] && cmp -s "$built_vst3" "$inst_vst3"; then inst_ok="installed VST3 == built VST3"
else inst_ok="**installed VST3 differs from this build**"; fail=1; fi
fp+=("| installed VST3 | \`$(shasum -a 256 "$inst_vst3" 2>/dev/null | cut -c1-16)\` | $inst_ok |")

# --- 2. gates -------------------------------------------------------------------
gate "AutoPitchCoreContract (incl. AutoKey strength parity)" "$(bin AutoPitchCoreContract)"
gate "AutoPitchRender strict golden null"  "$(bin AutoPitchRender)"
gate "AutoPitchRender --smoke"             "$(bin AutoPitchRender)" --smoke
gate "AutoPitchRender --nulltest"          "$(bin AutoPitchRender)" --nulltest
gate "AutoPitchRender --livetest"          "$(bin AutoPitchRender)" --livetest
rm -f "$HOME/Library/Sattari Audio/cross-plugin/key-bus.json"   # --livetest leaves a bus key behind
gate "AutoPitchObjectiveBench"             "$(bin AutoPitchObjectiveBench)"
if [ "$NOHARM_MINUTES" -lt 0 ]; then   # -1: no-harm matrix run separately (cite its logs + binary sha)
  rows+=("| AutoPitchNoHarm | run separately (NOHARM_MINUTES=-1); see the no-harm logs cited in this report's commit | - |")
elif [ "$NOHARM_MINUTES" -gt 0 ]; then
  gate "AutoPitchNoHarm (no-harm matrix, ${NOHARM_MINUTES} min soak)" "$(bin AutoPitchNoHarm)" --long-minutes "$NOHARM_MINUTES"
else   # NOHARM_MINUTES=0: checks 1-7 here; run the soak separately (--check 8) and cite its log
  gate "AutoPitchNoHarm checks 1-7 (soak run separately)" "$(bin AutoPitchNoHarm)" --check 1 --check 2 --check 3 --check 4 --check 5 --check 6 --check 7
fi
gate "auval aufx Ap01 Satr"                sh -c 'auval -v aufx Ap01 Satr 2>&1 | tee /dev/stderr | grep -q "AU VALIDATION SUCCEEDED"'
if [ -x /Applications/pluginval.app/Contents/MacOS/pluginval ]; then
  gate "pluginval strictness 5 (VST3)"     /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate-in-process --skip-gui-tests --validate "$VST3"
else rows+=("| pluginval strictness 5 | SKIPPED (pluginval not installed) | - |"); fi
if "$PY" -c 'import pedalboard' 2>/dev/null; then
  gate "$LOAD_CYCLES load/process/unload cycles" sh -c "'$PY' scripts/plugin_load_loop.py '$VST3' $LOAD_CYCLES | grep -q ok"
else rows+=("| load/unload cycles | SKIPPED (pedalboard missing for $PY) | - |"); fi
lat_check() {
  "$PY" - "$LOG/tone.wav" <<'PY'
import sys, wave, struct, math
w = wave.open(sys.argv[1], 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
w.writeframes(b''.join(struct.pack('<h', int(8000 * math.sin(2 * math.pi * 220 * i / 48000))) for i in range(48000)))
PY
  "$(bin AutoPitchBench)" "$LOG/tone.wav" --trace "$LOG/tone.json" --block 512 >/dev/null 2>&1 || return 1
  got="$("$PY" -c "import json;print(json.load(open('$LOG/tone.json'))['reportedLatencySamples'])")"
  echo "reported latency $got (expected $EXPECTED_LATENCY)"; [ "$got" = "$EXPECTED_LATENCY" ]
}
gate "reported latency == $EXPECTED_LATENCY samples" lat_check

# --- 3. report -------------------------------------------------------------------
{
  echo "# Auto Pitch qualification $DATE, $(git rev-parse HEAD)"
  echo
  echo "Generated by \`scripts/qualify_auto_pitch.sh $B\`. Free disk at start: ${free_gb} GB."
  if [ -n "$dirty" ]; then echo; echo "**Working tree NOT clean** (results are not evidence for $SHA):"; echo '```'; echo "$dirty"; echo '```'; fi
  echo; echo "## Binaries (rebuilt by this run; a second build did no work)"; echo
  echo "| binary | sha256 (16) | built / note |"; echo "|---|---|---|"; printf '%s\n' "${fp[@]}"
  echo; echo "## Gates"; echo; echo "| gate | result | time |"; echo "|---|---|---|"; printf '%s\n' "${rows[@]}"
  echo; echo "Result: $([ $fail -eq 0 ] && echo '**ALL MACHINE GATES PASS**' || echo '**FAILED**')"
} > "$REPORT"
echo "report: $REPORT"; exit $fail
