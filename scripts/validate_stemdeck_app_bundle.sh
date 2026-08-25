#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: validate_stemdeck_app_bundle.sh <package-dir|Sattari StemDeck.app>

Validates the standalone StemDeck macOS app bundle for local alpha release gates:
- app bundle and executable exist;
- ad-hoc/deep codesign verification passes;
- Info.plist is a macOS APPL bundle with optional EXPECTED_STEMDECK_APP_VERSION;
- binary has no absolute /opt/homebrew dylib links;
- @rpath Rubber Band / libsamplerate / ONNX links are backed by bundled Frameworks dylibs;
- binary has @executable_path/../Frameworks rpath when @rpath dylibs are used;
- bundled dylibs do not point back to /opt/homebrew and expose @rpath install ids.
EOF
}

if [[ $# -ne 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 2
fi

input="$1"
if [[ -d "$input" && "${input##*.}" == "app" ]]; then
  app="$input"
elif [[ -d "$input/Sattari StemDeck/Sattari StemDeck.app" ]]; then
  app="$input/Sattari StemDeck/Sattari StemDeck.app"
elif [[ -d "$input" ]]; then
  app="$(find "$input" -maxdepth 4 -type d -name 'Sattari StemDeck.app' -print -quit)"
  if [[ -z "$app" ]]; then
    echo "Could not find Sattari StemDeck.app under $input" >&2
    exit 1
  fi
else
  echo "Input is not a directory: $input" >&2
  exit 1
fi

binary="$app/Contents/MacOS/Sattari StemDeck"
plist="$app/Contents/Info.plist"
frameworks="$app/Contents/Frameworks"

fail() {
  echo "StemDeck app bundle validation failed: $*" >&2
  exit 1
}

[[ -d "$app" ]] || fail "missing app bundle at $app"
[[ -x "$binary" ]] || fail "missing executable at $binary"
[[ -f "$plist" ]] || fail "missing Info.plist at $plist"

codesign --verify --deep --strict --verbose=2 "$app"

bundle_type="$(/usr/libexec/PlistBuddy -c 'Print :CFBundlePackageType' "$plist" 2>/dev/null || true)"
[[ "$bundle_type" == "APPL" ]] || fail "CFBundlePackageType must be APPL, got ${bundle_type:-missing}"

bundle_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist" 2>/dev/null || true)"
[[ -n "$bundle_version" ]] || fail "missing CFBundleShortVersionString"
if [[ -n "${EXPECTED_STEMDECK_APP_VERSION:-}" && "$bundle_version" != "$EXPECTED_STEMDECK_APP_VERSION" ]]; then
  fail "expected version $EXPECTED_STEMDECK_APP_VERSION, got $bundle_version"
fi

# Capture-then-grep everywhere in this file, never `producer | grep -q`: this
# script runs under `set -o pipefail`, and `grep -q` exits as soon as it finds
# a match — closing its end of the pipe while the producer may still be
# writing. The producer then dies of SIGPIPE, pipefail sees that nonzero exit,
# and `||` fires even though the pattern WAS found. Confirmed empirically on
# the @executable_path check below: genuinely flaky, ~4 failures in 5 runs
# against a real bundle, despite the rpath being correctly present every time.
# `$(...)` command substitution fully captures output with no concurrent
# reader, so it can't race this way — apply it uniformly rather than judging
# per call site which producer's output is "probably short enough."
file_info="$(file "$binary")"
grep -q 'Mach-O' <<<"$file_info" || fail "executable is not a Mach-O binary"

binary_links="$(otool -L "$binary")"
if grep -q '/opt/homebrew' <<<"$binary_links"; then
  echo "$binary_links" >&2
  fail "app binary still references /opt/homebrew dylibs"
fi

uses_rubberband=false
uses_samplerate=false
uses_onnx=false
if grep -q '@rpath/librubberband\.3\.dylib' <<<"$binary_links"; then
  uses_rubberband=true
fi
if grep -q '@rpath/libsamplerate\.0\.dylib' <<<"$binary_links"; then
  uses_samplerate=true
fi
if grep -q '@rpath/libonnxruntime.*\.dylib' <<<"$binary_links"; then
  uses_onnx=true
fi

if [[ "$uses_rubberband" == true || "$uses_samplerate" == true || "$uses_onnx" == true ]]; then
  [[ -d "$frameworks" ]] || fail "@rpath dylibs are used but Contents/Frameworks is missing"
  binary_load_commands="$(otool -l "$binary")"
  grep -q '@executable_path/../Frameworks' <<<"$binary_load_commands" \
    || fail "binary uses @rpath dylibs but lacks @executable_path/../Frameworks rpath"
  absolute_rpaths="$(awk '
    /LC_RPATH/ { want=1; next }
    want && / path / {
      line=$0; sub(/^[[:space:]]*path /, "", line); sub(/ \(offset.*$/, "", line)
      if (line ~ /^\//) print line
      want=0
    }' <<<"$binary_load_commands")"
  [[ -z "$absolute_rpaths" ]] || fail "binary contains machine-local absolute rpath(s): $absolute_rpaths"
fi

check_bundled_dylib() {
  local dylib_name="$1"
  local dylib_path="$frameworks/$dylib_name"
  [[ -f "$dylib_path" ]] || fail "missing bundled dylib $dylib_name"
  local links
  links="$(otool -L "$dylib_path")"
  if grep -q '/opt/homebrew' <<<"$links"; then
    echo "$links" >&2
    fail "bundled $dylib_name still references /opt/homebrew"
  fi
  local install_id
  install_id="$(otool -D "$dylib_path" | tail -n +2 | head -1 | sed 's/^[[:space:]]*//')"
  [[ "$install_id" == "@rpath/$dylib_name" ]] || fail "$dylib_name install id must be @rpath/$dylib_name, got ${install_id:-missing}"
}

if [[ "$uses_rubberband" == true ]]; then
  check_bundled_dylib "librubberband.3.dylib"
fi
if [[ "$uses_samplerate" == true ]]; then
  check_bundled_dylib "libsamplerate.0.dylib"
fi
if [[ "$uses_onnx" == true ]]; then
  # Was `\\(...\\)`/`\\.`/`\\1` (double backslash). In a single-quoted bash
  # argument that reaches sed as a LITERAL backslash-paren / backslash-dot in a
  # BRE pattern — "match a literal backslash character" — which never occurs in
  # otool -L output, so this capture never matched and this check has never once
  # passed on a real ONNX-linked binary. Fourth instance of this exact escaping
  # mistake found in this codebase during this audit (see package_stemdeck_app_
  # alpha.sh's onnx_ref extraction for the other two, and NeuralBeatEngine.cpp's
  # jmax<size_t> for an unrelated one) — worth treating as a class of bug, not a
  # one-off: any `\\` inside a single-quoted argument to awk/sed/grep is
  # almost certainly not what was intended.
  onnx_name="$(sed -n 's|.*@rpath/\(libonnxruntime[^[:space:]]*\.dylib\).*|\1|p' <<<"$binary_links" | head -1)"
  [[ -n "$onnx_name" ]] || fail "could not resolve bundled ONNX Runtime name"
  check_bundled_dylib "$onnx_name"
fi

printf 'StemDeck app bundle validation OK\n'
printf 'app: %s\n' "$app"
printf 'version: %s\n' "$bundle_version"
printf 'rubberband_bundled: %s\n' "$uses_rubberband"
printf 'libsamplerate_bundled: %s\n' "$uses_samplerate"
printf 'onnxruntime_bundled: %s\n' "$uses_onnx"
