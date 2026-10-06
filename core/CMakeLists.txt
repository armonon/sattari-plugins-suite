if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(SattariAudioCore VERSION 0.1.0)
endif()

add_library(SattariAudioCore INTERFACE)

target_include_directories(SattariAudioCore
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(SattariAudioCore INTERFACE cxx_std_17)

if(MSVC)
    target_compile_definitions(SattariAudioCore INTERFACE _USE_MATH_DEFINES)
endif()

add_executable(SattariAudioCoreHarmonicContract
    tests/harmonic_core_contract.cpp)

target_link_libraries(SattariAudioCoreHarmonicContract
    PRIVATE
        SattariAudioCore)

# Shared pitch-detector contract (pure-std; no JUCE needed).
add_executable(SattariAudioCorePitchContract
    tests/pitch_detector_contract.cpp)

target_link_libraries(SattariAudioCorePitchContract
    PRIVATE
        SattariAudioCore)

# Shared 24-key Krumhansl-Schmuckler detector contract (pure-std; no JUCE needed).
add_executable(SattariAudioCoreKeyContract
    tests/key_detector_contract.cpp)

target_link_libraries(SattariAudioCoreKeyContract
    PRIVATE
        SattariAudioCore)

# Global BPM inference from a streaming spectral-flux/onset envelope (pure std).
add_executable(SattariAudioCoreTempoContract
    tests/tempo_estimator_contract.cpp)

target_link_libraries(SattariAudioCoreTempoContract
    PRIVATE
        SattariAudioCore)

# Key-profile benchmark tool — evidence for the default profile in
# SattariKeyDetector.h (Krumhansl vs Temperley vs Aarden-Essen). Pure-std; a tool,
# not a test, so EXCLUDE_FROM_ALL — build/run it explicitly to regenerate the table.
add_executable(SattariAudioCoreKeyBench EXCLUDE_FROM_ALL
    tools/key_detector_bench.cpp)

target_link_libraries(SattariAudioCoreKeyBench
    PRIVATE
        SattariAudioCore)

# Real-dataset key evaluator — scores the shared detector against a labeled
# corpus (GiantSteps) with the MIREX weighted metric, for all three profile
# sets. Needs JUCE for audio decode + FFT, so unlike the bench it links the juce
# modules (mirrors the SongKeyRender/contract targets, + audio_formats + dsp).
# A tool, not a test, so EXCLUDE_FROM_ALL — build/run it explicitly.
add_executable(SattariKeyDatasetEval EXCLUDE_FROM_ALL
    tools/key_dataset_eval.cpp)

target_include_directories(SattariKeyDatasetEval
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(SattariKeyDatasetEval
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Sattari Key — standalone batch key + Camelot + BPM analyzer (the DJ product).
# Runs the deployed keynet.onnx (needs ONNX) over a folder and writes a report.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/SattariOnnx.cmake)
add_executable(SattariKeyAnalyzer EXCLUDE_FROM_ALL
    tools/key_analyzer.cpp)

target_include_directories(SattariKeyAnalyzer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(SattariKeyAnalyzer
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

sattari_enable_onnx(SattariKeyAnalyzer SATTARI_HAS_ONNX)

# KeyAnalysisEngine evaluator. Scores the SHARED engine — chroma profiles AND the
# neural path — through one front-end and one scorer, so the 72.1 MIREX figure
# recorded in SattariKeyNetSpec.h can be reproduced rather than quoted. Needs
# ONNX for the neural path; without it the tool still scores every chroma profile.
add_executable(SattariKeyEngineEval EXCLUDE_FROM_ALL
    tools/key_engine_eval.cpp)

target_include_directories(SattariKeyEngineEval
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(SattariKeyEngineEval
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

sattari_enable_onnx(SattariKeyEngineEval SATTARI_HAS_ONNX)

# In-key interval walk contract (pure-std) — locks that a "third" is two SCALE steps,
# so its semitone size follows the degree, and that a chromatic key falls back to a
# fixed interval instead of degenerating into a whole tone.
add_executable(SattariAudioCoreScaleIntervalContract
    tests/scale_interval_contract.cpp)

target_link_libraries(SattariAudioCoreScaleIntervalContract
    PRIVATE
        SattariAudioCore)

# BS.1770 K-weighting + true-peak contract (pure-std). Shared by Meter and Peak,
# which each carried their own copy of these standards-defined filters.
add_executable(SattariAudioCoreKWeightingContract
    tests/kweighting_contract.cpp)

target_link_libraries(SattariAudioCoreKWeightingContract
    PRIVATE
        SattariAudioCore)

# Synthesised drum-voice engine contract (pure-std). Backs the Rhythm Genie beat
# machine: locks that every voice sounds + rings out, Decay lengthens tails, and
# brightness is ordered like real drums (kick dull, hats bright).
add_executable(SattariAudioCoreDrumKitContract
    tests/drum_kit_contract.cpp)

target_link_libraries(SattariAudioCoreDrumKitContract
    PRIVATE
        SattariAudioCore)

# Playable drum-pad bindings + learn-flow contract (pure-std). The "press Set, press
# anything, it's mapped" experience: MIDI/key bindings, one-touch learn, move-on-rebind.
add_executable(SattariAudioCoreDrumPadsContract
    tests/drum_pads_contract.cpp)

target_link_libraries(SattariAudioCoreDrumPadsContract
    PRIVATE
        SattariAudioCore)

# Tap-drum loop contract (pure-std). Feel-preserving looper: phase-recorded hits
# replay once per cycle, boundary wrap drops/doubles nothing, overdub adds hits.
add_executable(SattariAudioCoreDrumLooperContract
    tests/drum_looper_contract.cpp)

target_link_libraries(SattariAudioCoreDrumLooperContract
    PRIVATE
        SattariAudioCore)

# Editable step-grid contract (pure-std). Lanes x steps with on/velocity/probability,
# loop length = active step count, deterministic probability gating.
add_executable(SattariAudioCoreBeatGridContract
    tests/beat_grid_contract.cpp)

target_link_libraries(SattariAudioCoreBeatGridContract
    PRIVATE
        SattariAudioCore)

# Generative groove contract (pure-std). Euclidean rhythms, style generation, density
# thin/fill, deterministic mutate, stacks preserved on regenerate.
add_executable(SattariAudioCoreGrooveContract
    tests/groove_contract.cpp)

target_link_libraries(SattariAudioCoreGrooveContract
    PRIVATE
        SattariAudioCore)

# Note engine contract: the CENTS UNIFICATION. Twelve-tone and maqam scales
# become one data type, so "a third above, in this key" is one code path for a
# diatonic third and a Rast neutral third. Tested on both, every time.
add_executable(SattariAudioCoreNoteEngineContract
    tests/note_engine_contract.cpp)

target_link_libraries(SattariAudioCoreNoteEngineContract
    PRIVATE
        SattariAudioCore)

# Arrangement contract: phrase position, fill windows and edge-triggered section
# changes. This is what lets a generative rhythm fill BEFORE the chorus, which no
# context-free step sequencer can do.
add_executable(SattariAudioCoreArrangementContract
    tests/arrangement_contract.cpp)

target_link_libraries(SattariAudioCoreArrangementContract
    PRIVATE
        SattariAudioCore)

# Shared step clock contract. The clock is about to drive drums, notes AND
# parameter modulation, so a timing error would surface as three unrelated-looking
# bugs; this pins placement accuracy, block-size independence and swing.
add_executable(SattariAudioCoreStepClockContract
    tests/step_clock_contract.cpp)

target_link_libraries(SattariAudioCoreStepClockContract
    PRIVATE
        SattariAudioCore)

# Streaming TD-PSOLA pitch shifter contract (pure-std; no JUCE needed).
add_executable(SattariAudioCorePsolaContract
    tests/psola_shifter_contract.cpp)

target_link_libraries(SattariAudioCorePsolaContract
    PRIVATE
        SattariAudioCore)

# Next-generation causal tracker + waveform-locked TD-PSOLA foundation. This is
# intentionally an isolated core contract until its real-vocal gates beat the
# legacy live path; it is not connected to any shipping plugin control.
add_executable(SattariAudioCoreLivePitchContract
    tests/live_pitch_engine_contract.cpp)

target_link_libraries(SattariAudioCoreLivePitchContract
    PRIVATE
        SattariAudioCore)

# CREPE salience-decode contract (pure-std; ONNX inference itself is optional and
# not exercised here — this locks the bin<->Hz mapping + weighted-average decode).
add_executable(SattariAudioCoreCrepeContract
    tests/crepe_pitch_contract.cpp)

target_link_libraries(SattariAudioCoreCrepeContract
    PRIVATE
        SattariAudioCore)

add_executable(SattariAudioCoreOnnxPathContract
    tests/onnx_path_contract.cpp)

target_link_libraries(SattariAudioCoreOnnxPathContract
    PRIVATE
        SattariAudioCore)

# Conservative YIN + CREPE fusion contract. Locks agreement blending, neural
# octave rejection, conflict fallback, and high-confidence neural rescue.
add_executable(SattariAudioCorePitchFusionContract
    tests/pitch_fusion_contract.cpp)

target_link_libraries(SattariAudioCorePitchFusionContract
    PRIVATE
        SattariAudioCore)

# VoxSynth analysis front-end contract (pure-std; embedded FFT + shared YIN). Locks
# onset/legato/plosive classification, octave suppression, and percussion classes.
add_executable(SattariAudioCoreVoxContract
    tests/vox_analyzer_contract.cpp)

target_link_libraries(SattariAudioCoreVoxContract
    PRIVATE
        SattariAudioCore)

# VoxSynth continuous-pitch synth engine contract (pure-std).
add_executable(SattariAudioCoreVoxSynthContract
    tests/vox_synth_contract.cpp)

target_link_libraries(SattariAudioCoreVoxSynthContract
    PRIVATE
        SattariAudioCore)

# Formant shifter contract (pure-std; cepstral envelope warp preserves pitch).
add_executable(SattariAudioCoreFormantContract
    tests/formant_shifter_contract.cpp)

target_link_libraries(SattariAudioCoreFormantContract
    PRIVATE
        SattariAudioCore)

# Channel vocoder bank contract (pure-std; carrier takes the modulator's spectrum).
add_executable(SattariAudioCoreVocoderContract
    tests/vocoder_bank_contract.cpp)

target_link_libraries(SattariAudioCoreVocoderContract
    PRIVATE
        SattariAudioCore)

# Chop slice-table contract (pure-std; the one-way-door schema). Locked markers survive
# re-detection, tombstones stay deleted, sensitivity touches only unlocked markers.
add_executable(SattariAudioCoreSliceTableContract
    tests/slice_table_contract.cpp)

target_link_libraries(SattariAudioCoreSliceTableContract
    PRIVATE
        SattariAudioCore)

# Chop onset-curve contract (pure-std). Analyse-once + cheap re-threshold, markers on
# transients, monotonic sensitivity, refractory. Real-material proof is the separate
# chop_onset_probe tool (GiantSteps corpus) — synthetic input alone proves nothing.
add_executable(SattariAudioCoreOnsetCurveContract
    tests/onset_curve_contract.cpp)

target_link_libraries(SattariAudioCoreOnsetCurveContract
    PRIVATE
        SattariAudioCore)

# Causal harmonic/percussive soft-mask contract (pure-std). Backs Auto Pitch poly
# "Tonal Focus": tonal partials read harmonic (safe to tune), broadband transients read
# percussive (left unshifted). Synthetic input proves the algorithm; the perceptual mix
# result is the separate Auto Pitch --polyfile A/B on real material.
add_executable(SattariAudioCoreHarmonicMaskContract
    tests/harmonic_mask_contract.cpp)

target_link_libraries(SattariAudioCoreHarmonicMaskContract
    PRIVATE
        SattariAudioCore)

# Cross-thread snapshot contract. Exercises a large number of concurrent
# publications/reads and validates that no torn frame can escape.
add_executable(SattariAudioCoreRealtimeSnapshotContract
    tests/realtime_snapshot_contract.cpp)

target_link_libraries(SattariAudioCoreRealtimeSnapshotContract
    PRIVATE
        SattariAudioCore)

# NOTE: the cross-plugin session bus contract needs juce_core, which is not yet
# available when core/ is configured (plugins add JUCE after this). It is defined
# at the end of the root CMakeLists, once a JUCE target exists.
