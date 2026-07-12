cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_STEMDECK_PLUGIN VERSION 0.5.0)

# Tracktion Engine requires C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariTracktionEngine.cmake)

sattari_add_plugin(
    TARGET SattariStemDeck
    SLUG stemdeck
    PRODUCT_NAME "Sattari StemDeck"
    PLUGIN_CODE Sd01
    FORMATS VST3 Standalone
    IS_SYNTH
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/ArrangementBuilder.cpp
        src/BeatSyncEngine.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/HostGraph.cpp
        src/MainPluginEditor.cpp
        src/MainPluginProcessor.cpp
        src/MasterRecorder.cpp
        src/MixerSection.cpp
        src/MusicLibraryView.cpp
        src/PerformanceRecorder.cpp
        src/PadBank.cpp
        src/RoadmapAdapters.h
        src/SamplePad.cpp
        src/RealTimeStemSeparator.cpp
        src/NeuralStemEngine.cpp
        src/StemProvider.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp
        src/TransportSync.cpp
        src/WarpEngine.cpp
        src/WaveformDisplay.cpp)

# StemDeck additionally hosts plugins, so it needs the JUCE plugin-host flags
# on top of the shared packaging defaults.
target_compile_definitions(SattariStemDeck
    PUBLIC
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0)

sattari_enable_rubberband(SattariStemDeck SATTARI_STEMDECK_HAS_RUBBERBAND)
sattari_enable_tracktion_engine(SattariStemDeck)

# The EXCLUDE_FROM_ALL contract/benchmark executables below reuse SattariStemDeck's
# generated JuceHeader.h, which #includes <tracktion_graph/...>. Link the Tracktion
# module targets already created above (do NOT re-run sattari_enable_tracktion_engine
# — juce_add_module would redefine the targets) so those headers resolve + link.
function(stemdeck_link_tracktion TARGET)
    if(TARGET tracktion::tracktion_graph)
        target_link_libraries(${TARGET} PRIVATE
            tracktion::tracktion_graph
            tracktion::tracktion_core
            tracktion::tracktion_engine)
        set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
        set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD_REQUIRED ON)
        target_compile_definitions(${TARGET} PUBLIC TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0)
    endif()
endfunction()

# ── Optional: on-device neural stem separation via ONNX Runtime ───────────────
# Enables in-process, CoreML-accelerated neural demixing when ONNX Runtime is
# present. The app still builds + runs (RTSS + demucs-CLI fallback) without it.
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
          PATH_SUFFIXES onnxruntime
          PATHS /opt/homebrew/include /usr/local/include)
find_library(ONNXRUNTIME_LIB onnxruntime
             PATHS /opt/homebrew/lib /usr/local/lib)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    message(STATUS "StemDeck: ONNX Runtime found — on-device neural separation ENABLED (${ONNXRUNTIME_LIB})")
    target_include_directories(SattariStemDeck PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(SattariStemDeck PRIVATE ${ONNXRUNTIME_LIB})
    target_compile_definitions(SattariStemDeck PRIVATE SATTARI_STEMDECK_HAS_ONNX=1)
else()
    message(STATUS "StemDeck: ONNX Runtime not found — neural separation uses demucs-CLI + RTSS fallback")
endif()

add_executable(StemDeckGranularRenderContract
    EXCLUDE_FROM_ALL
        tests/stemdeck_granular_render_contract.cpp
        src/BeatSyncEngine.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp)

add_dependencies(StemDeckGranularRenderContract SattariStemDeck)
stemdeck_link_tracktion(StemDeckGranularRenderContract)

target_include_directories(StemDeckGranularRenderContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckGranularRenderContract
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckGranularRenderContract
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(StemDeckTimePitchBenchmark
    EXCLUDE_FROM_ALL
        tests/stemdeck_timepitch_benchmark.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp)

add_dependencies(StemDeckTimePitchBenchmark SattariStemDeck)
stemdeck_link_tracktion(StemDeckTimePitchBenchmark)

target_include_directories(StemDeckTimePitchBenchmark
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckTimePitchBenchmark
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckTimePitchBenchmark
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(StemDeckTimePitchBenchmark SATTARI_STEMDECK_HAS_RUBBERBAND)

add_executable(StemDeckArchitectureContract
    EXCLUDE_FROM_ALL
        tests/stemdeck_architecture_contract.cpp
        src/ArrangementBuilder.cpp
        src/BeatSyncEngine.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/HostGraph.cpp
        src/PerformanceRecorder.cpp
        src/RealTimeStemSeparator.cpp
        src/NeuralStemEngine.cpp
        src/StemProvider.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp
        src/WarpEngine.cpp)

add_dependencies(StemDeckArchitectureContract SattariStemDeck)
stemdeck_link_tracktion(StemDeckArchitectureContract)

target_include_directories(StemDeckArchitectureContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckArchitectureContract
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckArchitectureContract
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(StemDeckArchitectureContract SATTARI_STEMDECK_HAS_RUBBERBAND)

add_executable(StemDeckBeatDetectTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_beat_detect.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckBeatDetectTest SattariStemDeck)
target_include_directories(StemDeckBeatDetectTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckBeatDetectTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckBeatDetectTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckBeatDetectTest)

# ── Resampler quality check (linear vs windowed-sinc interpolation) ────────────
# Header-only (Interpolation.h) + juce_audio_basics; no engine deps.
add_executable(StemDeckResamplerTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_resampler.cpp)
add_dependencies(StemDeckResamplerTest SattariStemDeck)
target_include_directories(StemDeckResamplerTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckResamplerTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckResamplerTest
    PRIVATE juce::juce_audio_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ── Neural separation smoke test (real audio → 4 stems via ONNX Runtime) ───────
# Exercises the app's exact NeuralStemEngine + ORT path headlessly. Only built
# when ONNX Runtime is present.
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    add_executable(StemDeckNeuralSmoke
        EXCLUDE_FROM_ALL
            tests/stemdeck_neural_smoke.cpp
            src/NeuralStemEngine.cpp)

    add_dependencies(StemDeckNeuralSmoke SattariStemDeck)

    target_include_directories(StemDeckNeuralSmoke
        PRIVATE
            src
            ${ONNXRUNTIME_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

    target_compile_definitions(StemDeckNeuralSmoke
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_PLUGINHOST_AU=1
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_LV2=0
            JUCE_PLUGINHOST_VST=0
            JUCE_VST3_CAN_REPLACE_VST2=0
        PRIVATE
            SATTARI_STEMDECK_HAS_ONNX=1)

    target_link_libraries(StemDeckNeuralSmoke
        PRIVATE
            juce::juce_audio_utils
            ${ONNXRUNTIME_LIB}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    stemdeck_link_tracktion(StemDeckNeuralSmoke)
endif()
