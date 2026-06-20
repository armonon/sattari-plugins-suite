#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <Sattari Auto Pitch.component|.vst3|.app> [...]" >&2
  exit 2
fi

rubberband="$(brew --prefix rubberband)/lib/librubberband.3.dylib"
samplerate="$(brew --prefix libsamplerate)/lib/libsamplerate.0.dylib"

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
  codesign --force --deep --sign - "$bundle" >/dev/null
  codesign --verify --deep --strict --verbose=2 "$bundle"
  echo "bundled Auto Pitch runtime deps: $bundle"
done
