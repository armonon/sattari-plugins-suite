#!/usr/bin/env bash
# Fetch the pinned universal ONNX Runtime used by the Auto Pitch neural build.
# The extracted SDK is intentionally ignored by git; this script is the
# reproducible source of truth for both CI and local release builds.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ort_version="1.22.0"
ort_archive="onnxruntime-osx-universal2-${ort_version}.tgz"
ort_dir="$repo_root/third_party/onnxruntime-osx-universal2-${ort_version}"
ort_url="https://github.com/microsoft/onnxruntime/releases/download/v${ort_version}/${ort_archive}"
ort_sha256="cfa6f6584d87555ed9f6e7e8a000d3947554d589efe3723b8bfa358cd263d03c"
ort_dylib="$ort_dir/lib/libonnxruntime.${ort_version}.dylib"

if [[ -f "$ort_dir/include/onnxruntime_cxx_api.h" && -f "$ort_dylib" ]]; then
  echo "$ort_dir"
  exit 0
fi

download_tmp="$(mktemp -d "${TMPDIR:-/tmp}/sattari-ort.XXXXXX")"
trap 'rm -rf "$download_tmp"' EXIT

curl --fail --location --retry 3 --output "$download_tmp/$ort_archive" "$ort_url" >&2
printf '%s  %s\n' "$ort_sha256" "$download_tmp/$ort_archive" | shasum -a 256 --check >&2
tar -xzf "$download_tmp/$ort_archive" -C "$repo_root/third_party"

[[ -f "$ort_dir/include/onnxruntime_cxx_api.h" ]] || {
  echo "ONNX Runtime headers were not extracted to $ort_dir" >&2
  exit 1
}
[[ -f "$ort_dylib" ]] || {
  echo "ONNX Runtime library was not extracted to $ort_dylib" >&2
  exit 1
}

if command -v lipo >/dev/null 2>&1; then
  lipo "$ort_dylib" -verify_arch arm64 x86_64 >&2
fi

echo "$ort_dir"
