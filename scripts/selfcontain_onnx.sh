#!/usr/bin/env bash
# Generic ONNX self-contain: embed libonnxruntime into any plugin bundle that links
# it, repoint the @rpath at Contents/Frameworks, and codesign. Fixes the "rebuild
# silently drops the embedded runtime" gotcha for ANY plugin (auto-pitch, voxkey, ...).
#
# Usage: scripts/selfcontain_onnx.sh "<.component or .vst3>" [more bundles...]
set -euo pipefail
[[ $# -ge 1 ]] || { echo "usage: $0 <bundle.component|.vst3> [...]" >&2; exit 2; }

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
onnx="libonnxruntime.1.22.0.dylib"
onnx_src="$repo_root/third_party/onnxruntime-osx-universal2-1.22.0/lib/$onnx"

rpaths() { otool -l "$1" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}'; }

for bundle in "$@"; do
  [[ -d "$bundle" ]] || { echo "missing bundle: $bundle" >&2; exit 1; }
  # Derive the binary name from Contents/MacOS (one executable per bundle).
  binary="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f 2>/dev/null | head -1)"
  [[ -n "$binary" ]] || { echo "no binary in $bundle" >&2; exit 1; }

  if ! otool -L "$binary" | grep -q 'libonnxruntime'; then
    echo "skip (no onnx link): $bundle"
    continue
  fi
  [[ -f "$onnx_src" ]] || { echo "missing onnxruntime dylib: $onnx_src" >&2; exit 1; }

  fw="$bundle/Contents/Frameworks"
  mkdir -p "$fw"
  cp -f "$onnx_src" "$fw/$onnx"; chmod 755 "$fw/$onnx"
  install_name_tool -id "@rpath/$onnx" "$fw/$onnx"

  # Self-contained rpath (idempotent) + drop any absolute (repo) rpaths.
  rpaths "$binary" | grep -qx "@loader_path/../Frameworks" \
    || install_name_tool -add_rpath "@loader_path/../Frameworks" "$binary"
  while IFS= read -r rp; do
    case "$rp" in /*) install_name_tool -delete_rpath "$rp" "$binary" 2>/dev/null || true ;; esac
  done < <(rpaths "$binary")

  codesign --force --sign - "$fw/$onnx" >/dev/null
  codesign --force --deep --sign - "$bundle" >/dev/null
  codesign --verify --deep --strict "$bundle"
  echo "self-contained onnx: $bundle"
done
