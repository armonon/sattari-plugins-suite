cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUTO_PITCH_PLUGIN VERSION 0.4.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)

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

# Objective vocal-tuning benchmark suite (correction accuracy, latency/PDC, THD,
# CPU real-time factor, note-transition times, vibrato). Reported numbers, not pass/fail.
add_executable(AutoPitchBench
    EXCLUDE_FROM_ALL
        tools/auto_pitch_bench.cpp
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
        SATTARI_REPO_ROOT="${CMAKE_CURRENT_LIST_DIR}/../..")
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
