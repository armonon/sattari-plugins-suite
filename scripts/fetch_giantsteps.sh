#!/usr/bin/env bash
# Fetch a subset of the GiantSteps Key dataset (real EDM tracks + key labels) for
# real-dataset key-detection validation. Audio from the JKU backup mirror (Beatport
# previews are dead); labels from the GitHub annotations. Converts mp3 -> mono 44.1k
# wav (JUCE reads wav natively). Output is gitignored (validation/giantsteps/).
#
# Usage: scripts/fetch_giantsteps.sh [N]   (default 80 tracks)
set -uo pipefail
N="${1:-80}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/validation/giantsteps"
wavdir="$out/wav"
mkdir -p "$wavdir"
labels="$out/labels.csv"
: > "$labels"

api="https://api.github.com/repos/GiantSteps/giantsteps-key-dataset/contents/annotations/key?per_page=1000"
raw="https://raw.githubusercontent.com/GiantSteps/giantsteps-key-dataset/master/annotations/key"
audio="https://www.cp.jku.at/datasets/giantsteps/backup"

echo "listing annotations..."
names=$(curl -sL --max-time 40 "$api" | grep -oE '[0-9]+\.LOFI\.key' | sort -u | head -"$N")
total=$(echo "$names" | wc -l | tr -d ' ')
echo "fetching $total tracks -> $out"

i=0; ok=0
for name in $names; do
  i=$((i+1))
  id="${name%%.*}"
  wav="$wavdir/$id.wav"
  # label
  key=$(curl -sL --max-time 20 "$raw/$name" 2>/dev/null | head -1 | tr -d '\r' | xargs)
  [ -z "$key" ] && { echo "  [$i/$total] $id: no label, skip"; continue; }
  # audio (skip if already converted)
  if [ ! -s "$wav" ]; then
    mp3="/tmp/gs_$id.mp3"
    code=$(curl -s -o "$mp3" -w '%{http_code}' --max-time 60 "$audio/$id.LOFI.mp3" 2>/dev/null)
    if [ "$code" != "200" ] || [ ! -s "$mp3" ]; then echo "  [$i/$total] $id: audio $code, skip"; rm -f "$mp3"; continue; fi
    ffmpeg -y -loglevel error -i "$mp3" -ac 1 -ar 44100 "$wav" 2>/dev/null || { echo "  [$i/$total] $id: decode fail"; rm -f "$mp3"; continue; }
    rm -f "$mp3"
  fi
  echo "$id.wav,$key" >> "$labels"
  ok=$((ok+1))
  [ $((ok % 10)) -eq 0 ] && echo "  ...$ok ok"
done
echo "DONE: $ok/$total tracks with audio+label -> $labels"
