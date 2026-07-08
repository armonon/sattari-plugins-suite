#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <Sattari Auto Pitch.component|.vst3|.app> [...]" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for bundle in "$@"; do
  if [[ ! -d "$bundle" ]]; then
    echo "missing bundle: $bundle" >&2
    exit 1
  fi

  binary="$bundle/Contents/MacOS/Sattari Auto Pitch"
  frameworks="$bundle/Contents/Frameworks"
  if [[ ! -f "$binary" ]]; then
    echo "missing bundle binary: $binary" >&2
    exit 1
  fi

  mkdir -p "$frameworks"

  # Rubber Band / libsamplerate — only when the binary DYNAMICALLY links them. Current
  # builds vendor Rubber Band statically (cmake/SattariRubberBand.cmake), so these are
  # normally skipped; this branch keeps the script correct for any older dynamic build.
  if otool -L "$binary" | grep -q 'librubberband'; then
    rubberband="$(brew --prefix rubberband)/lib/librubberband.3.dylib"
    samplerate="$(brew --prefix libsamplerate)/lib/libsamplerate.0.dylib"
    cp -f "$rubberband" "$frameworks/librubberband.3.dylib"
    cp -f "$samplerate" "$frameworks/libsamplerate.0.dylib"
    chmod 755 "$frameworks/librubberband.3.dylib" "$frameworks/libsamplerate.0.dylib"

    install_name_tool -id "@loader_path/librubberband.3.dylib" "$frameworks/librubberband.3.dylib"
    install_name_tool -change "$samplerate" "@loader_path/libsamplerate.0.dylib" "$frameworks/librubberband.3.dylib"
    install_name_tool -id "@loader_path/libsamplerate.0.dylib" "$frameworks/libsamplerate.0.dylib"

    install_name_tool -change "$rubberband" "@loader_path/../Frameworks/librubberband.3.dylib" "$binary"
    install_name_tool -change "$samplerate" "@loader_path/../Frameworks/libsamplerate.0.dylib" "$binary" 2>/dev/null || true

    codesign --force --sign - "$frameworks/libsamplerate.0.dylib" >/dev/null
    codesign --force --sign - "$frameworks/librubberband.3.dylib" >/dev/null
  fi

  # ONNX Runtime (CREPE neural pitch) — only when the binary actually links it.
  # The binary references @rpath/libonnxruntime...; embed the dylib and repoint the
  # rpath at Contents/Frameworks so the plugin is self-contained (no repo/brew dep).
  if otool -L "$binary" | grep -q 'libonnxruntime'; then
    onnx_name="libonnxruntime.1.22.0.dylib"
    onnx_src="$repo_root/third_party/onnxruntime-osx-universal2-1.22.0/lib/$onnx_name"
    if [[ ! -f "$onnx_src" ]]; then
      echo "missing onnxruntime dylib: $onnx_src" >&2
      exit 1
    fi
    cp -f "$onnx_src" "$frameworks/$onnx_name"
    chmod 755 "$frameworks/$onnx_name"
    install_name_tool -id "@rpath/$onnx_name" "$frameworks/$onnx_name"
    # Add the self-contained rpath (idempotent) and drop any absolute repo rpaths.
    if ! otool -l "$binary" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | grep -qx "@loader_path/../Frameworks"; then
      install_name_tool -add_rpath "@loader_path/../Frameworks" "$binary"
    fi
    while IFS= read -r rp; do
      case "$rp" in
        /*) install_name_tool -delete_rpath "$rp" "$binary" 2>/dev/null || true ;;
      esac
    done < <(otool -l "$binary" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')
    codesign --force --sign - "$frameworks/$onnx_name" >/dev/null
  fi

  codesign --force --deep --sign - "$bundle" >/dev/null
  codesign --verify --deep --strict --verbose=2 "$bundle"
  echo "bundled Auto Pitch runtime deps: $bundle"
done
