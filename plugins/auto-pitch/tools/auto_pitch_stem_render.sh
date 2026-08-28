#!/bin/bash
# Auto Pitch — studio stem-render (poly-tune tier 3).
#
# The in-plugin poly engine (tiers 1-2) tunes a whole mix in real time and now follows
# tonal overtones into the top end (High Tracking) while protecting percussive/noise
# content (Tonal Focus). Its hard limit is that a single mixed spectrum cannot fully
# separate one instrument's partials from another's, so densely overlapping material can
# still beat. This OFFLINE mode removes that limit: it separates the instrumental into
# stems (Demucs), tunes each TONAL stem on its own — no cross-source interference — leaves
# the drum stem untouched, and remixes. It is a batch/render workflow, not a live plugin.
#
# It shells out to an already-installed Demucs and the Auto Pitch render tool. It does NOT
# touch StemDeck — separation here is a throwaway intermediate for tuning, nothing more.
#
# Usage:  auto_pitch_stem_render.sh <in.wav> <out.wav> [root 0..11] [scale 0=maj,1=min]
#   With no key, each stem auto-detects its own key (best for clean stems).
set -euo pipefail

IN="${1:?usage: auto_pitch_stem_render.sh <in.wav> <out.wav> [root scale]}"
OUT="${2:?output path required}"
ROOT="${3:-}"; SCALE="${4:-}"

DEMUCS="${DEMUCS:-$(command -v demucs || echo "$HOME/Library/Python/3.9/bin/demucs")}"
RENDER="${RENDER:-build/local/plugins/auto-pitch/AutoPitchRender}"
[ -x "$DEMUCS" ] || { echo "FATAL: demucs not found (set DEMUCS=/path/to/demucs)"; exit 2; }
[ -x "$RENDER" ] || { echo "FATAL: AutoPitchRender not built at $RENDER (set RENDER=...)"; exit 2; }

KEYARG=""
[ -n "$ROOT" ] && [ -n "$SCALE" ] && KEYARG="$ROOT $SCALE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo ">> separating stems (Demucs htdemucs) ..."
"$DEMUCS" -n htdemucs --float32 -o "$WORK" "$IN"

base="$(basename "${IN%.*}")"
STEMDIR="$WORK/htdemucs/$base"
[ -d "$STEMDIR" ] || { echo "FATAL: expected stems in $STEMDIR"; ls -R "$WORK/htdemucs" || true; exit 3; }

echo ">> tuning tonal stems (poly + High Tracking + Tonal Focus) ..."
tuned=()
for st in bass other vocals; do
  if [ -f "$STEMDIR/$st.wav" ]; then
    "$RENDER" --tunefile "$STEMDIR/$st.wav" "$WORK/${st}_tuned.wav" $KEYARG
    tuned+=("$WORK/${st}_tuned.wav")
  fi
done
# Drums pass through untouched (never pitch-corrected).
[ -f "$STEMDIR/drums.wav" ] && tuned+=("$STEMDIR/drums.wav")

echo ">> remixing ..."
"$RENDER" --mix "$OUT" "${tuned[@]}"
echo ">> stem-render complete -> $OUT"
