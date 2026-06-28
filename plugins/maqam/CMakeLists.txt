cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MAQAM_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariMaqam
    SLUG maqam
    PRODUCT_NAME "Sattari Maqam"
    PLUGIN_CODE Mq01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/MaqamProcessor.cpp
        src/MaqamEditor.cpp)

sattari_enable_rubberband(SattariMaqam SATTARI_MAQAM_HAS_RUBBERBAND)

# Pure-C++ microtonal contract (no JUCE; mirrors the other core gates).
add_executable(MaqamCoreContract EXCLUDE_FROM_ALL tests/maqam_core_contract.cpp)
target_include_directories(MaqamCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)

# Offline render + microtonal-correction proof (links the processor + JUCE).
# Default run measures the output pitch and asserts the quarter-tone snap;
# --smoke just asserts finite/non-silent (CI-safe across Rubber Band versions).
add_executable(MaqamRender
    EXCLUDE_FROM_ALL
        tools/maqam_render.cpp
        src/MaqamProcessor.cpp
        src/MaqamEditor.cpp)
add_dependencies(MaqamRender SattariMaqam)
target_include_directories(MaqamRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariMaqam_artefacts/JuceLibraryCode)
target_compile_definitions(MaqamRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MaqamRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
sattari_enable_rubberband(MaqamRender SATTARI_MAQAM_HAS_RUBBERBAND)
