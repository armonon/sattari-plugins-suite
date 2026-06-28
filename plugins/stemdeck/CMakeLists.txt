cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_STEMDECK_PLUGIN VERSION 0.4.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)

sattari_add_plugin(
    TARGET SattariStemDeck
    SLUG stemdeck
    PRODUCT_NAME "Sattari StemDeck"
    PLUGIN_CODE Sd01
    FORMATS AU VST3 Standalone
    IS_SYNTH
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
        src/PerformanceRecorder.cpp
        src/PadBank.cpp
        src/RoadmapAdapters.h
        src/SamplePad.cpp
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

# Optional ML stem separation (off by default; -DSATTARI_USE_ONNX=ON to enable).
# Defines SATTARI_HAS_ONNX for SattariStemSeparator.h; no-op when disabled.
sattari_enable_onnx(SattariStemDeck SATTARI_HAS_ONNX)

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
        src/StemProvider.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp
        src/WarpEngine.cpp)

add_dependencies(StemDeckArchitectureContract SattariStemDeck)

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
