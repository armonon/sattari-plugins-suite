cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUTO_PITCH_PLUGIN VERSION 0.5.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariSignalsmith.cmake)

# Keep the released print engine deterministic.  Previously simply initialising
# the optional Signalsmith submodule changed the DSP selected by the same source
# revision, so local builds and recursive-submodule CI builds did not render the
# same audio.  Rubber Band is the qualified 0.5.0 engine; Signalsmith remains an
# explicit experimental choice which must be requalified before release.
set(SATTARI_AUTOPITCH_PRINT_ENGINE "RUBBERBAND" CACHE STRING
    "Auto Pitch print engine: RUBBERBAND (qualified) or SIGNALSMITH (experimental)")
set_property(CACHE SATTARI_AUTOPITCH_PRINT_ENGINE PROPERTY STRINGS RUBBERBAND SIGNALSMITH)
string(TOUPPER "${SATTARI_AUTOPITCH_PRINT_ENGINE}" _sattari_auto_pitch_print_engine)
if(NOT _sattari_auto_pitch_print_engine STREQUAL "RUBBERBAND"
   AND NOT _sattari_auto_pitch_print_engine STREQUAL "SIGNALSMITH")
    message(FATAL_ERROR
        "Invalid SATTARI_AUTOPITCH_PRINT_ENGINE='${SATTARI_AUTOPITCH_PRINT_ENGINE}'. "
        "Choose RUBBERBAND or SIGNALSMITH.")
endif()

if(_sattari_auto_pitch_print_engine STREQUAL "RUBBERBAND")
    if(NOT SATTARI_USE_RUBBERBAND
       OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/rubberband/single/RubberBandSingle.cpp")
        message(FATAL_ERROR
            "Auto Pitch is pinned to RUBBERBAND, but the qualified engine is unavailable. "
            "Enable SATTARI_USE_RUBBERBAND and initialise third_party/rubberband.")
    endif()
    set(_sattari_auto_pitch_print_signalsmith 0)
else()
    if(NOT SATTARI_USE_SIGNALSMITH
       OR NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../third_party/signalsmith-stretch/signalsmith-stretch.h")
        message(FATAL_ERROR
            "Auto Pitch is pinned to SIGNALSMITH, but the experimental engine is unavailable. "
            "Enable SATTARI_USE_SIGNALSMITH and initialise third_party/signalsmith-stretch.")
    endif()
    set(_sattari_auto_pitch_print_signalsmith 1)
endif()

function(sattari_configure_auto_pitch_print_engine target)
    target_compile_definitions(${target} PRIVATE
        SATTARI_AUTOPITCH_PRINT_SIGNALSMITH=${_sattari_auto_pitch_print_signalsmith})
endfunction()

set(SATTARI_AUTOPITCH_ARA_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ARA_SDK"
    CACHE PATH "Path to the ARA SDK 2.2 checkout")
if(EXISTS "${SATTARI_AUTOPITCH_ARA_SDK_PATH}/ARA_API")
    set(_sattari_ara_default ON)
else()
    set(_sattari_ara_default OFF)
endif()
option(SATTARI_AUTOPITCH_ENABLE_ARA "Build the Auto Pitch ARA2 extension" ${_sattari_ara_default})

set(SATTARI_AAX_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../third_party/AAX_SDK"
    CACHE PATH "Path to a licensed Avid AAX SDK checkout")
option(SATTARI_AUTOPITCH_ENABLE_AAX "Build Auto Pitch in AAX format for Pro Tools" OFF)

set(_sattari_formats AU VST3 Standalone)
set(_sattari_ara_args)
set(_sattari_extra_sources)
if(SATTARI_AUTOPITCH_ENABLE_ARA)
    if(NOT EXISTS "${SATTARI_AUTOPITCH_ARA_SDK_PATH}/ARA_API")
        message(FATAL_ERROR "ARA2 requested but the SDK was not found. Run scripts/fetch_ara_sdk.sh or set SATTARI_AUTOPITCH_ARA_SDK_PATH.")
    endif()
    list(APPEND _sattari_ara_args IS_ARA_EFFECT)
    list(APPEND _sattari_extra_sources src/AutoPitchARA.cpp)
endif()

if(SATTARI_AUTOPITCH_ENABLE_AAX)
    if(NOT EXISTS "${SATTARI_AAX_SDK_PATH}")
        message(FATAL_ERROR "AAX requested but Avid's licensed SDK was not found. Set SATTARI_AAX_SDK_PATH to your SDK checkout.")
    endif()
    list(APPEND _sattari_formats AAX)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariElastique.cmake)

sattari_add_plugin(
    TARGET SattariAutoPitch
    SLUG auto-pitch
    PRODUCT_NAME "Sattari Auto Pitch"
    PLUGIN_CODE Ap01
    VERSION ${PROJECT_VERSION}
    EDITOR_WANTS_KEYBOARD_FOCUS
    ${_sattari_ara_args}
    ARA_SDK_PATH ${SATTARI_AUTOPITCH_ARA_SDK_PATH}
    AAX_SDK_PATH ${SATTARI_AAX_SDK_PATH}
    FORMATS ${_sattari_formats}
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        ${_sattari_extra_sources})

# The MIT CREPE-tiny model is embedded so Neural Pitch is a real, portable
# capability rather than a machine-specific toggle. ONNX-less builds link no
# inference code and the linker drops the unreferenced model object.
juce_add_binary_data(SattariAutoPitchModelData
    HEADER_NAME SattariAutoPitchModelData.h
    NAMESPACE SattariAutoPitchModelData
    SOURCES
        models/crepe.onnx
        models/CREPE-LICENSE.txt)
target_link_libraries(SattariAutoPitch PRIVATE SattariAutoPitchModelData)

sattari_enable_rubberband(SattariAutoPitch SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_onnx(SattariAutoPitch SATTARI_HAS_ONNX)
sattari_enable_elastique(SattariAutoPitch SATTARI_AUTOPITCH_HAS_ELASTIQUE)
sattari_enable_signalsmith(SattariAutoPitch SATTARI_AUTOPITCH_HAS_SIGNALSMITH)
sattari_configure_auto_pitch_print_engine(SattariAutoPitch)

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
        SattariAutoPitchModelData
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(AutoPitchCoreContract SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_onnx(AutoPitchCoreContract SATTARI_HAS_ONNX)
sattari_enable_elastique(AutoPitchCoreContract SATTARI_AUTOPITCH_HAS_ELASTIQUE)
sattari_enable_signalsmith(AutoPitchCoreContract SATTARI_AUTOPITCH_HAS_SIGNALSMITH)
sattari_configure_auto_pitch_print_engine(AutoPitchCoreContract)

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
        SATTARI_REPO_ROOT="${CMAKE_CURRENT_LIST_DIR}/../..")

target_link_libraries(AutoPitchRender
    PRIVATE
        SattariAutoPitchModelData
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(AutoPitchRender SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_onnx(AutoPitchRender SATTARI_HAS_ONNX)
sattari_enable_elastique(AutoPitchRender SATTARI_AUTOPITCH_HAS_ELASTIQUE)
sattari_enable_signalsmith(AutoPitchRender SATTARI_AUTOPITCH_HAS_SIGNALSMITH)
sattari_configure_auto_pitch_print_engine(AutoPitchRender)

# TWO benches, deliberately. Both sides of the merge defined a target called
# AutoPitchBench from different sources, so neither is dropped:
#
#   AutoPitchBench          main's headless driver — takes real audio files and
#                           writes corrected.wav + trace.json. The name is
#                           hard-coded in scripts/vocalbench_run.py, so it keeps it.
#   AutoPitchObjectiveBench the objective suite — correction accuracy, latency/PDC,
#                           THD, CPU real-time factor, transitions, vibrato. Its
#                           CLOCK_THREAD_CPUTIME_ID measurement is what withdrew a
#                           false "Studio mode is too slow" blocker, so it survives
#                           under its own name rather than being replaced.
add_executable(AutoPitchBench
    EXCLUDE_FROM_ALL
        tools/autopitch_bench.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(AutoPitchBench SattariAutoPitch)

add_executable(AutoPitchObjectiveBench
    EXCLUDE_FROM_ALL
        tools/auto_pitch_bench.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(AutoPitchObjectiveBench SattariAutoPitch)
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
        SattariAutoPitchModelData
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
sattari_enable_rubberband(AutoPitchBench SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_onnx(AutoPitchBench SATTARI_HAS_ONNX)
sattari_enable_elastique(AutoPitchBench SATTARI_AUTOPITCH_HAS_ELASTIQUE)
sattari_enable_signalsmith(AutoPitchBench SATTARI_AUTOPITCH_HAS_SIGNALSMITH)
sattari_configure_auto_pitch_print_engine(AutoPitchBench)

# The objective suite mirrors the bench's configuration exactly, so the two
# measure the same engine rather than two differently-configured builds.
target_compile_definitions(AutoPitchObjectiveBench
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SATTARI_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(AutoPitchObjectiveBench
    PRIVATE
        SattariAutoPitchModelData
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
target_include_directories(AutoPitchObjectiveBench
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariAutoPitch_artefacts/JuceLibraryCode)
sattari_enable_rubberband(AutoPitchObjectiveBench SATTARI_AUTOPITCH_HAS_RUBBERBAND)
sattari_enable_onnx(AutoPitchObjectiveBench SATTARI_HAS_ONNX)
# Both benches get the same engine configuration, or they measure different builds.
sattari_enable_elastique(AutoPitchObjectiveBench SATTARI_AUTOPITCH_HAS_ELASTIQUE)
sattari_enable_signalsmith(AutoPitchObjectiveBench SATTARI_AUTOPITCH_HAS_SIGNALSMITH)
sattari_configure_auto_pitch_print_engine(AutoPitchObjectiveBench)

# Phase 3 — deterministic motion-estimator bench (pure C++17, no JUCE).
# Tests all 5 estimators × 5 window sizes against synthetic trajectories.
# Run: autopitch_motionbench [--window-ms W] [--estimator E]
# Exit 0 ⟹ best RMSE ≤ 2 MIDI/sec ⟹ enable kSlideDestinationV2Enabled.
add_executable(AutoPitchMotionBench
    EXCLUDE_FROM_ALL
        tools/autopitch_motionbench.cpp)

if(SATTARI_AUTOPITCH_ENABLE_ARA)
    add_executable(AutoPitchARAContract EXCLUDE_FROM_ALL
        ../song-key/tests/arrangement_ara_contract.cpp
        ../../third_party/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp)
    target_include_directories(AutoPitchARAContract PRIVATE $<TARGET_PROPERTY:SattariAutoPitch,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AutoPitchARAContract PRIVATE
        $<TARGET_PROPERTY:SattariAutoPitch,COMPILE_DEFINITIONS> JucePlugin_Build_Standalone=1 SATTARI_TEST_AUTOPITCH=1)
    target_link_libraries(AutoPitchARAContract PRIVATE SattariAutoPitch)
endif()

target_include_directories(AutoPitchMotionBench
    PRIVATE
        src)

target_compile_features(AutoPitchMotionBench PRIVATE cxx_std_17)

target_compile_options(AutoPitchMotionBench PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-O2 -Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>)
