#!/usr/bin/env bash
# Fetch the vendored ONNX Runtime that StemDeck's neural features link against.
#
# WHY THIS EXISTS
#
# Homebrew's libonnxruntime is built with a macOS 26.0 deployment floor. Linking
# it produces a binary that CLAIMS the suite's floor while actually requiring
# macOS 26 — it fails to load for nearly every user. cmake/SattariOnnx.cmake
# detects that and hard-errors on a shipping build, which is why SATTARI_USE_ONNX
# has been OFF and the whole neural stack has been compiled out of what ships.
#
# The official onnxruntime release is universal2 (arm64 + x86_64) with a 13.3
# floor, which is a real floor we can honour. third_party/onnxruntime-* is
# gitignored (a 68 MB dylib does not belong in git), so this script is how a
# fresh clone and CI obtain it.
#
#   scripts/fetch_onnxruntime.sh            # default version, into third_party/
#   ORT_VERSION=1.22.0 scripts/fetch_onnxruntime.sh
#
# Then configure with:
#   cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DSATTARI_USE_ONNX=ON
#
# StemDeck's CMakeLists points ONNXRUNTIME_ROOT at the extracted directory
# automatically, so no extra flag is needed.

set -euo pipefail

ORT_VERSION="${ORT_VERSION:-1.22.0}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest_parent="${repo_root}/third_party"
name="onnxruntime-osx-universal2-${ORT_VERSION}"
dest="${dest_parent}/${name}"
url="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${name}.tgz"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script fetches the macOS universal2 build; on other platforms install" >&2
  echo "onnxruntime through your package manager and set ONNXRUNTIME_ROOT." >&2
  exit 1
fi

if [[ -x "${dest}/lib/libonnxruntime.dylib" || -f "${dest}/lib/libonnxruntime.dylib" ]]; then
  echo "ONNX Runtime already present: ${dest}"
else
  mkdir -p "${dest_parent}"
  tmp="$(mktemp -d)"
  trap 'rm -rf "${tmp}"' EXIT
  echo "Downloading ${url}"
  curl --fail --location --progress-bar "${url}" -o "${tmp}/ort.tgz"
  tar -xzf "${tmp}/ort.tgz" -C "${dest_parent}"
  echo "Extracted to ${dest}"
fi

lib="${dest}/lib/libonnxruntime.dylib"
if [[ ! -e "${lib}" ]]; then
  echo "ERROR: ${lib} is missing after extraction" >&2
  exit 1
fi

# Report the two properties that actually matter, because getting either wrong is
# what put the project in this position: the architectures we can ship, and the
# real minimum-OS the linked binary will inherit.
echo
echo "archs: $(lipo -archs "${lib}" 2>/dev/null || echo unknown)"
# Capture first, then awk over the captured text — not `otool -l | awk '...exit}'`.
# This script runs under `set -euo pipefail` (line 26); an awk action ending in
# `exit` closes its input as soon as it matches, which can SIGPIPE the still-
# writing otool and, under pipefail, abort this whole script on a plain
# informational lookup. Confirmed as a real, reproducible race for this exact
# "early-exiting reader | late-writing producer" shape elsewhere in this repo's
# packaging scripts — see validate_stemdeck_app_bundle.sh's header comment.
load_commands="$(otool -l "${lib}" 2>/dev/null || true)"
minos="$(awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}' <<<"${load_commands}")"
echo "minos: ${minos:-unknown}"
echo
echo "StemDeck raises its own deployment floor to match this when SATTARI_USE_ONNX=ON."
echo "Configure with: cmake -S plugins/stemdeck -B build/stemdeck -G Ninja -DSATTARI_USE_ONNX=ON"
