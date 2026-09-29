#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
products=("Sattari Arp" "Sub Conjurer" "Sattari Mix" "Sattari Vocal" "Sattari Create")
for name in "${products[@]}"; do
    codesign --verify --deep --strict "$root/AU/$name.component"
    codesign --verify --deep --strict "$root/VST3/$name.vst3"
    if [[ "$name" != "Sattari Arp" ]]; then
        codesign --verify --deep --strict "$root/Standalone/$name.app"
    fi
done
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"
for name in "${products[@]}"; do
    ditto "$root/AU/$name.component" "$HOME/Library/Audio/Plug-Ins/Components/$name.component"
    ditto "$root/VST3/$name.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/$name.vst3"
    if [[ "$name" != "Sattari Arp" ]]; then
        ditto "$root/Standalone/$name.app" "$HOME/Applications/$name.app"
    fi
done
printf '%s\n' 'Installed five Sattari candidates. Restart/rescan your DAW.'
