#!/usr/bin/env bash
# =============================================================================
#  Sattari Audio — one-command local verification
# -----------------------------------------------------------------------------
#  Builds every contract + the render driver from the root build tree and runs
#  them, so "is the suite green?" is one command instead of ten. This is the
#  DSP-correctness net (synthetic-signal contracts + render null-test). It does
#  NOT sign / auval / notarize - that is scripts/validate_plugin.sh per product,
#  and real-material listening is still required before any sale-ready claim.
#
#  Usage:
#    scripts/verify_all.sh           # build + run everything, strict render null
#    scripts/verify_all.sh --smoke   # render in smoke mode (skip golden compare)
#    scripts/verify_all.sh --bless   # re-bless the render golden, then verify
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD="build/all"
MODE="${1:-}"

# target  -> built binary path (under $BUILD)
CONTRACTS=(
  "SattariAudioCorePitchContract|core/SattariAudioCorePitchContract"
  "SattariAudioCoreHarmonicContract|core/SattariAudioCoreHarmonicContract"
  "SattariCrossPluginBusContract|SattariCrossPluginBusContract"
  "AutoPitchCoreContract|plugins/auto-pitch/AutoPitchCoreContract"
  "VoxKeyCoreContract|plugins/voxkey/VoxKeyCoreContract"
  "SattariArpTimingContract|plugins/sattari-arp/SattariArpTimingContract"
  "SattariBusCoreContract|plugins/bus/SattariBusCoreContract"
  "SideChainMasterCoreContract|plugins/side-chain-master/SideChainMasterCoreContract"
  "SattariAlignCoreContract|plugins/align/SattariAlignCoreContract"
  "RoyalChainColorAliasingContract|plugins/royal-chain/RoyalChainColorAliasingContract"
  "StemDeckArchitectureContract|plugins/stemdeck/StemDeckArchitectureContract"
  "StemDeckGranularRenderContract|plugins/stemdeck/StemDeckGranularRenderContract"
  "HarmonyCoreContract|plugins/harmony/HarmonyCoreContract"
  "MaqamCoreContract|plugins/maqam/MaqamCoreContract"
  "RhythmGenieCoreContract|plugins/rhythm-genie/RhythmGenieCoreContract"
  "VocalMasterCoreContract|plugins/vocal-master/VocalMasterCoreContract"
  "SongKeyRender|plugins/song-key/SongKeyRender"
  "SubConjurerContract|plugins/sub-conjurer/SubConjurerContract"
  "WarpContract|plugins/warp/WarpContract"
  "TunerContract|plugins/tuner/TunerContract"
  "DoubleContract|plugins/double/DoubleContract"
  "DeEssContract|plugins/deess/DeEssContract"
  "FaceContract|plugins/face/FaceContract"
  "TransientContract|plugins/transient/TransientContract"
  "MeterContract|plugins/meter/MeterContract"
  "EQContract|plugins/eq/EQContract"
  "ScalerContract|plugins/scaler/ScalerContract"
  "SpaceContract|plugins/space/SpaceContract"
  "CleanContract|plugins/clean/CleanContract"
)

echo "== configuring $BUILD =="
cmake -S . -B "$BUILD" >/dev/null || { echo "configure FAILED"; exit 2; }

targets=()
for entry in "${CONTRACTS[@]}"; do targets+=("${entry%%|*}"); done
targets+=("AutoPitchRender")

echo "== building ${#targets[@]} targets =="
if ! cmake --build "$BUILD" -j --target "${targets[@]}" >/tmp/verify_all_build.log 2>&1; then
  echo "BUILD FAILED — see /tmp/verify_all_build.log"; grep -iE "error:" /tmp/verify_all_build.log | head; exit 2
fi

pass=0; fail=0
run() { # name binary [args...]
  local name="$1"; shift; local bin="$1"; shift
  if [ ! -x "$bin" ]; then printf "  MISS  %s\n" "$name"; fail=$((fail+1)); return; fi
  if "$bin" "$@" >/dev/null 2>&1; then printf "  PASS  %s\n" "$name"; pass=$((pass+1));
  else printf "  FAIL  %s\n" "$name"; fail=$((fail+1)); fi
}

echo "== running contracts =="
for entry in "${CONTRACTS[@]}"; do
  run "${entry%%|*}" "$BUILD/${entry##*|}"
done

echo "== render harness =="
RENDER="$BUILD/plugins/auto-pitch/AutoPitchRender"
case "$MODE" in
  --bless) "$RENDER" --bless >/dev/null 2>&1 && echo "  (golden re-blessed)"; run "AutoPitchRender(null)" "$RENDER" ;;
  --smoke) run "AutoPitchRender(smoke)" "$RENDER" --smoke ;;
  *)       run "AutoPitchRender(null)"  "$RENDER" ;;
esac
# Input-drive / output-trim split guard: output gain must be a clean post-Style
# trim, never folded into the saturator drive (portable — no golden compare).
run "AutoPitchRender(gainstage)" "$RENDER" --gainstage

echo "-------------------------------------------"
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ] || { echo "  VERIFY FAILED"; exit 1; }
echo "  VERIFY OK (DSP correctness net; not a sale-ready claim)"
