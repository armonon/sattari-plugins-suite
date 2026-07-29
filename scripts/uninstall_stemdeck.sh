#!/usr/bin/env bash
# Remove only the two installed StemDeck products. User projects, recordings,
# snapshots, library metadata, and preferences are deliberately preserved.
set -euo pipefail

targets=(
  "/Applications/Sattari StemDeck.app"
  "/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3"
  "$HOME/Applications/Sattari StemDeck.app"
  "$HOME/Library/Audio/Plug-Ins/VST3/Sattari StemDeck.vst3"
)

found=0
for target in "${targets[@]}"; do
  if [[ -e "$target" ]]; then
    found=1
    if [[ -w "$(dirname "$target")" ]]; then
      /bin/rm -rf -- "$target"
    else
      sudo /bin/rm -rf -- "$target"
    fi
    echo "Removed $target"
  fi
done

if [[ "$found" -eq 0 ]]; then
  echo "StemDeck was not installed in the standard app or VST3 locations."
fi
echo "Recordings, projects, snapshots, and preferences were preserved."
