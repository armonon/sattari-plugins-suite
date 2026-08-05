#!/usr/bin/env bash
# Release-critical self-containment check for a neural Auto Pitch bundle.
set -euo pipefail

bundle="${1:?usage: verify_auto_pitch_bundle.sh <component|vst3|app>}"
[[ -d "$bundle" ]] || { echo "bundle not found: $bundle" >&2; exit 2; }

binary="$bundle/Contents/MacOS/Sattari Auto Pitch"
frameworks="$bundle/Contents/Frameworks"
onnx_dylib="$frameworks/libonnxruntime.1.22.0.dylib"

[[ -f "$binary" ]] || { echo "plugin binary not found: $binary" >&2; exit 1; }
[[ -f "$onnx_dylib" ]] || { echo "embedded ONNX Runtime not found: $onnx_dylib" >&2; exit 1; }

otool -L "$binary" | grep -q '@rpath/libonnxruntime.1.22.0.dylib' || {
  echo "plugin does not link the pinned ONNX Runtime through @rpath" >&2
  exit 1
}
otool -L "$onnx_dylib" | grep -q '@rpath/libonnxruntime.1.22.0.dylib' || {
  echo "embedded ONNX Runtime has an unexpected install name" >&2
  exit 1
}

rpaths="$(otool -l "$binary" | awk '/LC_RPATH/{found=1} found&&/ path /{print $2; found=0}')"
grep -qx '@loader_path/../Frameworks' <<<"$rpaths" || {
  echo "plugin is missing its self-contained Frameworks rpath" >&2
  exit 1
}
if grep -q '^/' <<<"$rpaths"; then
  echo "plugin contains an absolute build-machine rpath:" >&2
  grep '^/' <<<"$rpaths" >&2
  exit 1
fi

if otool -L "$binary" "$onnx_dylib" | awk '/^\t/{print $1}' \
    | grep -E '/Users/|/opt/homebrew|/usr/local' >/dev/null; then
  echo "bundle contains a development-machine library reference" >&2
  exit 1
fi

if command -v lipo >/dev/null 2>&1; then
  lipo "$binary" -verify_arch arm64 x86_64
  lipo "$onnx_dylib" -verify_arch arm64 x86_64
fi

codesign --verify --deep --strict --verbose=2 "$bundle"
echo "Auto Pitch neural bundle verified: $bundle"
