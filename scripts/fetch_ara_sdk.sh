#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "${script_dir}/.." && pwd)"
destination="${repo_dir}/third_party/ARA_SDK"

if [[ -d "${destination}/ARA_API" ]]; then
    echo "ARA SDK 2.2 is already available at ${destination}"
    exit 0
fi

git clone --recursive --branch releases/2.2.0 --depth 1 \
    https://github.com/Celemony/ARA_SDK.git "${destination}"
echo "ARA SDK installed at ${destination}"
