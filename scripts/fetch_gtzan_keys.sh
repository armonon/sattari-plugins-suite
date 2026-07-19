#!/usr/bin/env bash
# Fetch the GTZAN audio (marsyas mirror on HuggingFace) + the Lerch key annotations,
# for KEY-DETECTION training. GTZAN adds ~837 tonal tracks across 9 genres — the genre
# DIVERSITY that a pure-EDM corpus (GiantSteps) lacks. Output is gitignored
# (validation/gtzan/); combine with GiantSteps via scripts/build_key_corpus.py.
#
# Lerch key convention is 0=A (0..11 major A..G#, 12..23 minor, -1 = no key), verified
# empirically (Krumhansl agrees 51% under 0=A vs 1% under 0=C). build_key_corpus.py
# converts it to the plugin's 0=C convention.
#
# Usage: scripts/fetch_gtzan_keys.sh
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/validation/gtzan"
mkdir -p "$out"

audio="https://huggingface.co/datasets/marsyas/gtzan/resolve/main/data/genres.tar.gz"
keys="https://github.com/alexanderlerch/gtzan_key/archive/refs/heads/master.tar.gz"

if [ ! -d "$out/genres" ]; then
  echo "downloading GTZAN audio (~1.2 GB) ..."
  curl -L --fail --max-time 1800 -o "$out/genres.tar.gz" "$audio" || { echo "audio download failed"; exit 1; }
  echo "extracting audio ..."
  tar xzf "$out/genres.tar.gz" -C "$out/" && rm -f "$out/genres.tar.gz"
fi

if [ ! -d "$out/keys" ]; then
  echo "downloading Lerch key annotations ..."
  curl -L --fail --max-time 120 -o /tmp/gtzan_key.tar.gz "$keys" || { echo "labels download failed"; exit 1; }
  mkdir -p "$out/keys"
  tar xzf /tmp/gtzan_key.tar.gz -C "$out/keys" --strip-components=1 && rm -f /tmp/gtzan_key.tar.gz
fi

wavs=$(find "$out/genres" -name '*.wav' | wc -l | tr -d ' ')
labs=$(find "$out/keys" -name '*.lerch.txt' | wc -l | tr -d ' ')
echo "GTZAN ready: $wavs wavs, $labs key labels -> $out"
