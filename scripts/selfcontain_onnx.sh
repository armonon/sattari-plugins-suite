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

rpaths() {
  # Preserve spaces in paths (the repository is commonly named "StemDeck copy").
  # `$2` truncated such paths and made the absolute-rpath cleanup a silent no-op.
  otool -l "$1" | awk '
    /LC_RPATH/ { want=1; next }
    want && / path / {
      line=$0
      sub(/^[[:space:]]*path /, "", line)
      sub(/ \(offset.*$/, "", line)
      print line
      want=0
    }'
}

for bundle in "$@"; do
  [[ -d "$bundle" ]] || { echo "missing bundle: $bundle" >&2; exit 1; }
  # Prefer the bundle's declared executable. Standalone StemDeck also contains
  # the plugin-sandbox helper, so "first file in Contents/MacOS" can select the
  # helper and incorrectly conclude that the app has no ONNX dependency.
  executable_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
      "$bundle/Contents/Info.plist" 2>/dev/null || true)"
  binary="${executable_name:+$bundle/Contents/MacOS/$executable_name}"
  if [[ -z "$binary" || ! -f "$binary" ]]; then
    binary="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f 2>/dev/null | head -1)"
  fi
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

  # Self-contained rpath (idempotent) + drop any absolute (repo) rpaths. App
  # executables use @executable_path; plug-ins use @loader_path because their
  # code is loaded into an unrelated host executable.
  desired_rpath="@loader_path/../Frameworks"
  if [[ "$bundle" == *.app ]]; then
    desired_rpath="@executable_path/../Frameworks"
    if rpaths "$binary" | grep -qx "@loader_path/../Frameworks"; then
      install_name_tool -delete_rpath "@loader_path/../Frameworks" "$binary"
    fi
  fi
  rpaths "$binary" | grep -qx "$desired_rpath" \
    || install_name_tool -add_rpath "$desired_rpath" "$binary"
  while IFS= read -r rp; do
    case "$rp" in /*) install_name_tool -delete_rpath "$rp" "$binary" 2>/dev/null || true ;; esac
  done < <(rpaths "$binary")

  codesign --force --sign - "$fw/$onnx" >/dev/null
  codesign --force --deep --sign - "$bundle" >/dev/null
  codesign --verify --deep --strict "$bundle"
  echo "self-contained onnx: $bundle"
done
