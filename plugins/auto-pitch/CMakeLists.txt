cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUTO_PITCH_PLUGIN VERSION 0.1.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariElastique.cmake)

sattari_add_plugin(
    TARGET SattariAutoPitch
    SLUG auto-pitch
    PRODUCT_NAME "Sattari Auto Pitch"
    PLUGIN_CODE Ap01
    VERSION ${PROJECT_VERSION}
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

sattari_enable_rubberband(SattariAutoPitch SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_elastique(SattariAutoPitch SATTARI_AUTOPITCH_HAS_ELASTIQUE)

add_executable(AutoPitchCoreContract
    EXCLUDE_FROM_ALL
        tests/auto_pitch_core_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(AutoPitchCoreContract SattariAutoPitch)

target_include_directories(AutoPitchCoreContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariAutoPitch_artefacts/JuceLibraryCode)

target_compile_definitions(AutoPitchCoreContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AutoPitchCoreContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(AutoPitchCoreContract SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_elastique(AutoPitchCoreContract SATTARI_AUTOPITCH_HAS_ELASTIQUE)

# Offline render + null-test driver (regression net on the actual sound).
add_executable(AutoPitchRender
    EXCLUDE_FROM_ALL
        tools/auto_pitch_render.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(AutoPitchRender SattariAutoPitch)

target_include_directories(AutoPitchRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariAutoPitch_artefacts/JuceLibraryCode)

target_compile_definitions(AutoPitchRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SATTARI_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(AutoPitchRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(AutoPitchRender SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_elastique(AutoPitchRender SATTARI_AUTOPITCH_HAS_ELASTIQUE)

# Headless bench driver — same production engine as the plugin, no UI.
# Accepts real audio files and writes corrected.wav + trace.json.
add_executable(AutoPitchBench
    EXCLUDE_FROM_ALL
        tools/autopitch_bench.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(AutoPitchBench SattariAutoPitch)

target_include_directories(AutoPitchBench
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariAutoPitch_artefacts/JuceLibraryCode)

target_compile_definitions(AutoPitchBench
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SATTARI_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(AutoPitchBench
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(AutoPitchBench SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_elastique(AutoPitchBench SATTARI_AUTOPITCH_HAS_ELASTIQUE)

# Phase 3 — deterministic motion-estimator bench (pure C++17, no JUCE).
# Tests all 5 estimators × 5 window sizes against synthetic trajectories.
# Run: autopitch_motionbench [--window-ms W] [--estimator E]
# Exit 0 ⟹ best RMSE ≤ 2 MIDI/sec ⟹ enable kSlideDestinationV2Enabled.
add_executable(AutoPitchMotionBench
    EXCLUDE_FROM_ALL
        tools/autopitch_motionbench.cpp)

target_include_directories(AutoPitchMotionBench
    PRIVATE
        src)

target_compile_features(AutoPitchMotionBench PRIVATE cxx_std_17)

target_compile_options(AutoPitchMotionBench PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-O2 -Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>)
