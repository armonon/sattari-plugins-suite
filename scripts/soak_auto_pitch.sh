#!/usr/bin/env bash
# =============================================================================
#  Auto Pitch 2-hour playback soak against a FROZEN candidate (operator run, ~15 min wall).
#  Refuses to run unless (1) the plugin sources at HEAD are identical to the candidate commit
#  and (2) the AutoPitchNoHarm binary's SHA-256 matches the one recorded in that commit's
#  qualification report. Never rebuilds (a rebuild would change the fingerprint). On finish it
#  saves the log next to the report and appends the result to the report.
#
#  Usage: [SOAK_SR=44100] scripts/soak_auto_pitch.sh <candidate sha> [minutes=120] [build dir=build/auto-pitch-neural]
#  SOAK_SR (default 48000) is the host rate; 44.1 kHz exercises the resampled Studio path.
#  The binary must match the report's "AutoPitchNoHarm" row, or its "AutoPitchNoHarm (soak tool)" row
#  when the tool was rebuilt after a tools-only change (plugin sources identical to the candidate).
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
C="${1:?usage: soak_auto_pitch.sh <candidate sha> [minutes] [build dir]}"; MIN="${2:-120}"; B="${3:-build/auto-pitch-neural}"
C="$(git rev-parse --short "$C")" || exit 2
[ "$MIN" -ge 60 ] || { echo "ABORT: a qualification soak is >= 60 minutes"; exit 2; }
REPORT="$(ls validation/auto-pitch/qualification/*-"$C".md 2>/dev/null | head -n1)"
[ -n "$REPORT" ] || { echo "ABORT: no qualification report for $C"; exit 2; }
changed="$(git diff --name-only "$C" HEAD -- plugins core cmake CMakeLists.txt third_party | grep -v -E '^plugins/auto-pitch/(tests|tools)/' || true)"
[ -z "$changed" ] || { echo "ABORT: plugin sources differ from $C:"; echo "$changed"; exit 2; }
BIN="$B/AutoPitchNoHarm"
SR="${SOAK_SR:-48000}"
want="$(grep -E '^\| AutoPitchNoHarm( \(soak tool\))? \| `' "$REPORT" | sed -E 's/.*`([0-9a-f]+)`.*/\1/')"
have="$(shasum -a 256 "$BIN" 2>/dev/null | cut -c1-16)"
[ -n "$have" ] && echo "$want" | grep -qx "$have" || { echo "ABORT: $BIN sha256 $have not in report's NoHarm rows ($want)"; exit 2; }
LOG="${REPORT%.md}-noharm-soak${MIN}-${SR}.log"
echo "soak: $MIN min at $SR Hz, candidate $C, binary $have -> $LOG"
NOHARM_SOAK_SR="$SR" caffeinate -i "$BIN" --check 8 --long-minutes "$MIN" | tee "$LOG"; rc=${PIPESTATUS[0]}
{ echo; echo "## ${MIN}-minute soak at ${SR} Hz ($(date '+%Y-%m-%d %H:%M'), AutoPitchNoHarm \`$have\`)"; echo
  echo "$([ $rc -eq 0 ] && echo PASS || echo '**FAIL**'): \`$(basename "$LOG")\`"; grep -E '^PASS|^FAIL|block time|RSS' "$LOG" | sed 's/^ */- /'; } >> "$REPORT"
echo "result appended to $REPORT (exit $rc)"; exit $rc
