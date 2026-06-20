#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio - reusable plugin machine-validation path
# -----------------------------------------------------------------------------
#  The codesign + auval + pluginval gate, lifted out of the per-plugin scripts so
#  every product validates through one path. This is a MACHINE gate only: it
#  proves the bundle is signed and the plugin is well-behaved (pluginval
#  strictness 5). It is NOT a sale-ready claim - manual DAW smoke + real-material
#  listening are still required per docs/plugin-build-standard.md.
#
#  Usage:
#     scripts/validate_plugin.sh <artefacts-Release-dir> [AU_TYPE AU_SUBTYPE [AU_MANUFACTURER]]
#
#  Examples:
#     scripts/validate_plugin.sh build/sub-conjurer/SubConjurer_artefacts/Release aufx Sc01 Satr
#     scripts/validate_plugin.sh build/stemdeck/SattariStemDeck_artefacts/Release
# =============================================================================
set -euo pipefail

dir="${1:?usage: validate_plugin.sh <artefacts Release dir> [auType auSubtype [auManufacturer]]}"
au_type="${2:-}"
au_sub="${3:-}"
au_manu="${4:-Satr}"
fail=0

echo "== Sattari plugin validation (machine gate only; not a sale-ready claim) =="
echo "   target dir: $dir"

if [ ! -d "$dir" ]; then
  echo "ERROR: artefacts dir not found: $dir" >&2
  exit 2
fi

# 1) codesign every built bundle (AU component, VST3, Standalone app).
found_any=0
while IFS= read -r bundle; do
  found_any=1
  echo "== codesign --verify: $bundle =="
  codesign --verify --deep --strict --verbose=2 "$bundle" || fail=1
done < <(find "$dir" -maxdepth 4 \( -name '*.component' -o -name '*.vst3' -o -name '*.app' \) 2>/dev/null)
if [ "$found_any" -eq 0 ]; then
  echo "WARNING: no .component/.vst3/.app bundles found under $dir"
fi

# 2) auval, if an AU was built and the type/subtype were provided.
if [ -n "$au_type" ] && [ -n "$au_sub" ]; then
  if command -v auval >/dev/null 2>&1; then
    echo "== auval -v $au_type $au_sub $au_manu =="
    auval -v "$au_type" "$au_sub" "$au_manu" || fail=1
  else
    echo "auval not available on this machine; skipping AU validation"
  fi
else
  echo "no AU type/subtype passed; skipping auval (pass e.g. 'aufx Sc01 Satr')"
fi

# 3) pluginval at strictness 5 on every built VST3.
pv="/Applications/pluginval.app/Contents/MacOS/pluginval"
if [ -x "$pv" ]; then
  while IFS= read -r vst3; do
    echo "== pluginval --strictness-level 5: $vst3 =="
    "$pv" --validate-in-process --strictness-level 5 "$vst3" || fail=1
  done < <(find "$dir" -maxdepth 4 -name '*.vst3' 2>/dev/null)
else
  echo "pluginval not installed at $pv; skipping (install to gate VST3 hygiene)"
fi

if [ "$fail" -ne 0 ]; then
  echo "VALIDATION FAILED (machine gate)"
  exit 1
fi
echo "VALIDATION PASSED (machine gate). Manual DAW + real-material listening still required before any sale-ready language."
