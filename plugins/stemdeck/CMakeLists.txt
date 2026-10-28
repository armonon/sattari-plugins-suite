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
# Routed through the shared helper, like every other neural-enabled target in
# the suite. This block used to do its own UNCONDITIONAL find_library over
# /opt/homebrew/lib with no opt-in and no architecture check, so on any machine
# with a Homebrew onnxruntime installed StemDeck silently linked it - and since
# that formula is arm64-only while the suite builds universal (arm64;x86_64 via
# SattariDeploymentTarget), the x86_64 slice failed to link with "Undefined
# symbols for architecture x86_64". The plugin could not be built at all on such
# a machine, for a feature that is off by default everywhere else.
#
# sattari_enable_onnx() is OFF unless -DSATTARI_USE_ONNX=ON and prefers
# ONNXRUNTIME_ROOT (the bundled universal runtime) over system paths. Without it
# StemDeck keeps its demucs-CLI + RTSS fallback, which is the path that actually
# works today.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)
sattari_enable_onnx(SattariStemDeck SATTARI_STEMDECK_HAS_ONNX)

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

# ── Key-detection accuracy check (chroma + tuning comp. + profile correlation) ─
add_executable(StemDeckKeyDetectTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_key_detect.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckKeyDetectTest SattariStemDeck)
target_include_directories(StemDeckKeyDetectTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckKeyDetectTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckKeyDetectTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckKeyDetectTest)

# ── Automix planner check (Camelot + phrase mix points + tempo fold) ──────────
add_executable(StemDeckAutomixTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_automix.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckAutomixTest SattariStemDeck)
target_include_directories(StemDeckAutomixTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckAutomixTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckAutomixTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckAutomixTest)

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

# ── Header-level resampling safety (no plugin or model runtime required) ──────
add_executable(StemDeckComponentStoragePolicy EXCLUDE_FROM_ALL
    tests/stemdeck_component_storage_policy.cpp)
target_include_directories(StemDeckComponentStoragePolicy PRIVATE src)
target_compile_definitions(StemDeckComponentStoragePolicy PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckComponentStoragePolicy PRIVATE
    juce::juce_core juce::juce_recommended_config_flags)

add_executable(StemDeckInterpolationBoundaries EXCLUDE_FROM_ALL
    tests/stemdeck_interpolation_boundaries.cpp)
target_include_directories(StemDeckInterpolationBoundaries PRIVATE src)
target_compile_definitions(StemDeckInterpolationBoundaries PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckInterpolationBoundaries PRIVATE
    juce::juce_audio_basics juce::juce_recommended_config_flags)

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
