#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
name="sattari-five-ui-candidates-2026-09-28"
out="$repo/deliverables/sattari-test-builds"
package="$out/$name"
[[ ! -e "$package" && ! -e "$package.tar.gz" ]] || { printf '%s\n' 'Package already exists.' >&2; exit 1; }
python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); assert len(r)==50 and all(x["exit_code"]==0 for x in r), "Qualification incomplete or failed"' "$here/results.json"
mkdir -p "$package/AU" "$package/VST3" "$package/Standalone" "$package/validation"

slugs=(sattari-arp sub-conjurer mix vocal create)
targets=(SattariArp SubConjurer SattariMix SattariVocal SattariCreate)
products=("Sattari Arp" "Sub Conjurer" "Sattari Mix" "Sattari Vocal" "Sattari Create")
for i in 0 1 2 3 4; do
    artifacts="$repo/build/mix-automix/${slugs[$i]}/${targets[$i]}_artefacts/Release"
    for format in AU VST3 Standalone; do
        [[ "$i" == 0 && "$format" == Standalone ]] && continue
        case "$format" in AU) extension=component ;; VST3) extension=vst3 ;; Standalone) extension=app ;; esac
        bundle="${products[$i]}.$extension"
        codesign --verify --deep --strict "$artifacts/$format/$bundle"
        ditto "$artifacts/$format/$bundle" "$package/$format/$bundle"
    done
done
cp "$repo/LICENSE" "$repo/LICENSING.md" "$package/"
cp "$repo/assets/icons/LICENSE.lucide" "$package/"
cp "$here/install_user_plugins.sh" "$package/"
cp "$here/PACKAGE.md" "$package/README.md"
ditto "$here" "$package/validation"
cp "$repo/build/mix-workspace/CMakeLists.txt" "$package/validation/local-workspace.CMakeLists.txt"

# Include the working-tree sources, including changes that are not committed.
tar --exclude=.git --exclude=.DS_Store --exclude=__pycache__ -czf "$package/source-snapshot.tar.gz" -C "$repo" \
    CMakeLists.txt cmake core plugins assets third_party/JUCE third_party/rubberband scripts docs LICENSE LICENSING.md README.md
(
    cd "$package"
    find AU VST3 Standalone -type f -print0 | xargs -0 shasum -a 256 > SHA256SUMS
    shasum -a 256 source-snapshot.tar.gz >> SHA256SUMS
)
tar -czf "$package.tar.gz" -C "$out" "$name"
printf '%s\n' "$package.tar.gz"
