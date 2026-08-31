#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build/stemdeck-overhaul}"
output_dir="${2:-${repo_root}/build/stemdeck-surfaces}"

# The JUCE File contract intentionally rejects ambiguous relative paths in debug
# builds. Make caller-provided paths absolute so the render gate stays assertion-
# free whether it is launched from the repo root or CI's working directory.
if [[ "${build_dir}" != /* ]]; then build_dir="${repo_root}/${build_dir}"; fi
if [[ "${output_dir}" != /* ]]; then output_dir="${repo_root}/${output_dir}"; fi

cmake --build "${build_dir}" --target StemDeckEditorRender -j "${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
mkdir -p "${output_dir}"

renderer="${build_dir}/StemDeckEditorRender"
for surface in decks library mixer arranger performance; do
  "${renderer}" "${output_dir}/${surface}.png" 1680 1050 4 0 0 1 "${surface}"
done
"${renderer}" "${output_dir}/master-output.png" 1680 1050 4 0 0 1 performance 0 master-output

# Source editors are a separate performance workflow from DJ decks. Gate the
# focused INPUT, TONE and FX states at the smallest supported working size so
# their tabs and safety controls cannot silently disappear or overlap.
"${renderer}" "${output_dir}/focused-source.png"      1180 760 1 1 0 0 performance 0 focus-source
"${renderer}" "${output_dir}/focused-source-tone.png" 1180 760 1 1 0 0 performance 0 focus-source-tone
"${renderer}" "${output_dir}/focused-source-fx.png"   1180 760 1 1 0 0 performance 0 focus-source-fx

echo "Rendered primary surfaces to ${output_dir}"
