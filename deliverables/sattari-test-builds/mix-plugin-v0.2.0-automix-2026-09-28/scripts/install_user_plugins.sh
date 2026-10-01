#!/usr/bin/env bash
set -euo pipefail

artifact_root="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
for bundle in 'AU/Sattari Mix.component' 'VST3/Sattari Mix.vst3' 'Standalone/Sattari Mix.app'; do
    if [[ ! -d "$artifact_root/$bundle" ]]; then
        echo "Missing bundle: $artifact_root/$bundle" >&2
        exit 1
    fi
    codesign --verify --deep --strict "$artifact_root/$bundle"
done

mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Applications"
ditto "$artifact_root/AU/Sattari Mix.component" "$HOME/Library/Audio/Plug-Ins/Components/Sattari Mix.component"
ditto "$artifact_root/VST3/Sattari Mix.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/Sattari Mix.vst3"
ditto "$artifact_root/Standalone/Sattari Mix.app" "$HOME/Applications/Sattari Mix.app"
echo "Installed Sattari Mix. Rescan plugins or restart the DAW to load this build."
