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
#    scripts/verify_all.sh --full    # also compile every non-StemDeck product
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
  "SattariAudioCoreKeyContract|core/SattariAudioCoreKeyContract"
  "SattariAudioCoreScaleIntervalContract|core/SattariAudioCoreScaleIntervalContract"
  "SattariAudioCoreKWeightingContract|core/SattariAudioCoreKWeightingContract"
  "SattariAudioCoreDrumKitContract|core/SattariAudioCoreDrumKitContract"
  "SattariAudioCoreDrumPadsContract|core/SattariAudioCoreDrumPadsContract"
  "SattariAudioCoreDrumLooperContract|core/SattariAudioCoreDrumLooperContract"
  "SattariAudioCoreBeatGridContract|core/SattariAudioCoreBeatGridContract"
  "SattariAudioCoreGrooveContract|core/SattariAudioCoreGrooveContract"
  "SattariAudioCoreStepClockContract|core/SattariAudioCoreStepClockContract"
  "SattariAudioCoreArrangementContract|core/SattariAudioCoreArrangementContract"
  "SattariAudioCoreNoteEngineContract|core/SattariAudioCoreNoteEngineContract"
  "SattariAudioCorePsolaContract|core/SattariAudioCorePsolaContract"
  "SattariAudioCoreCrepeContract|core/SattariAudioCoreCrepeContract"
  "SattariAudioCoreVoxContract|core/SattariAudioCoreVoxContract"
  "SattariAudioCoreVoxSynthContract|core/SattariAudioCoreVoxSynthContract"
  "SattariAudioCoreFormantContract|core/SattariAudioCoreFormantContract"
  "SattariAudioCoreVocoderContract|core/SattariAudioCoreVocoderContract"
  "SattariAudioCoreSliceTableContract|core/SattariAudioCoreSliceTableContract"
  "SattariAudioCoreOnsetCurveContract|core/SattariAudioCoreOnsetCurveContract"
  "SattariAudioCoreHarmonicMaskContract|core/SattariAudioCoreHarmonicMaskContract"
  "SattariAudioCoreRealtimeSnapshotContract|core/SattariAudioCoreRealtimeSnapshotContract"
  "SattariCrossPluginBusContract|SattariCrossPluginBusContract"
  "SattariRackHostContract|SattariRackHostContract"
  "AutoPitchCoreContract|plugins/auto-pitch/AutoPitchCoreContract"
  "VoxKeyCoreContract|plugins/voxkey/VoxKeyCoreContract"
  "SattariArpTimingContract|plugins/sattari-arp/SattariArpTimingContract"
  "SattariBusCoreContract|plugins/bus/SattariBusCoreContract"
  "SideChainMasterCoreContract|plugins/side-chain-master/SideChainMasterCoreContract"
  "SattariAlignCoreContract|plugins/align/SattariAlignCoreContract"
  "RoyalChainColorAliasingContract|plugins/royal-chain/RoyalChainColorAliasingContract"
  "HarmonyCoreContract|plugins/harmony/HarmonyCoreContract"
  "MaqamCoreContract|plugins/maqam/MaqamCoreContract"
  "RhythmGenieCoreContract|plugins/rhythm-genie/RhythmGenieCoreContract"
  "RhythmGenieTimingContract|plugins/rhythm-genie/RhythmGenieTimingContract"
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
  "CompContract|plugins/comp/CompContract"
  "PeakContract|plugins/peak/PeakContract"
  "VocoderContract|plugins/vocoder/VocoderContract"
  "ShimmerContract|plugins/shimmer/ShimmerContract"
  "EchoContract|plugins/echo/EchoContract"
  "HeatContract|plugins/heat/HeatContract"
  "MultibandContract|plugins/multiband/MultibandContract"
  "GateContract|plugins/gate/GateContract"
  "RiserContract|plugins/riser/RiserContract"
  "PulseContract|plugins/pulse/PulseContract"
  "Sattari808Contract|plugins/eightoheight/Sattari808Contract"
  "ImagerContract|plugins/imager/ImagerContract"
  "WarbleContract|plugins/warble/WarbleContract"
  "ScopeContract|plugins/scope/ScopeContract"
  "GranulatorContract|plugins/granulator/GranulatorContract"
  "GlassChoirContract|plugins/glass-choir/GlassChoirContract"
  "BrainContract|plugins/brain/BrainContract"
  "VoxSynthMpeContract|plugins/voxsynth/VoxSynthMpeContract"
  "SattariStackLayerContract|plugins/stack/SattariStackLayerContract"
)

echo "== configuring $BUILD =="
cmake -S . -B "$BUILD" >/dev/null || { echo "configure FAILED"; exit 2; }

if [ "$MODE" = "--full" ]; then
  echo "== compiling every configured non-StemDeck product =="
  if ! cmake --build "$BUILD" -j >/tmp/verify_all_suite_build.log 2>&1; then
    echo "SUITE BUILD FAILED — see /tmp/verify_all_suite_build.log"
    grep -iE "error:" /tmp/verify_all_suite_build.log | head
    exit 2
  fi
  MODE="--smoke"
fi

targets=()
for entry in "${CONTRACTS[@]}"; do targets+=("${entry%%|*}"); done
targets+=("AutoPitchRender" "AutoPitchObjectiveBench")

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
# Poly High Tracking: a corrected note's overtones above 2.5 kHz must follow it into
# key (self-contained; the +4.46 pt corpus win, locked against silent regression).
run "AutoPitchRender(hitrack)" "$RENDER" --hitracktest

# Objective scorecard is a real gate: every qualified row that prints FAIL must
# return non-zero. Unsupported stress points remain visible as OUTSIDE.
run "AutoPitchObjectiveBench" "$BUILD/plugins/auto-pitch/AutoPitchObjectiveBench"

echo "-------------------------------------------"
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ] || { echo "  VERIFY FAILED"; exit 1; }
echo "  VERIFY OK (DSP correctness net; not a sale-ready claim)"
