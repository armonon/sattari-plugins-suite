#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build/stemdeck-overhaul}"
output_dir="${2:-${repo_root}/build/stemdeck-surfaces}"

cmake --build "${build_dir}" --target StemDeckEditorRender -j "${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
mkdir -p "${output_dir}"

renderer="${build_dir}/StemDeckEditorRender"
for surface in decks library mixer arranger performance; do
  "${renderer}" "${output_dir}/${surface}.png" 1680 1050 4 0 0 1 "${surface}"
done

echo "Rendered primary surfaces to ${output_dir}"
