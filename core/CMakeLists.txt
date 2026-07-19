if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(SattariAudioCore VERSION 0.1.0)
endif()

add_library(SattariAudioCore INTERFACE)

target_include_directories(SattariAudioCore
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(SattariAudioCore INTERFACE cxx_std_17)

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

# Streaming TD-PSOLA pitch shifter contract (pure-std; no JUCE needed).
add_executable(SattariAudioCorePsolaContract
    tests/psola_shifter_contract.cpp)

target_link_libraries(SattariAudioCorePsolaContract
    PRIVATE
        SattariAudioCore)

# CREPE salience-decode contract (pure-std; ONNX inference itself is optional and
# not exercised here — this locks the bin<->Hz mapping + weighted-average decode).
add_executable(SattariAudioCoreCrepeContract
    tests/crepe_pitch_contract.cpp)

target_link_libraries(SattariAudioCoreCrepeContract
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

# NOTE: the cross-plugin session bus contract needs juce_core, which is not yet
# available when core/ is configured (plugins add JUCE after this). It is defined
# at the end of the root CMakeLists, once a JUCE target exists.
