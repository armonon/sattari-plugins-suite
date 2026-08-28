cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_HARMONY_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariHarmony
    SLUG harmony
    PRODUCT_NAME "Sattari Harmony"
    PLUGIN_CODE Hm01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/HarmonyProcessor.cpp
        src/HarmonyEditor.cpp)

sattari_enable_rubberband(SattariHarmony SATTARI_HARMONY_HAS_RUBBERBAND)

# Pure-C++ diatonic-voicing contract (no JUCE; mirrors the other core gates).
add_executable(HarmonyCoreContract EXCLUDE_FROM_ALL tests/harmony_core_contract.cpp)
target_include_directories(HarmonyCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)

# Offline render + "the harmony is really there" proof (links processor + JUCE).
add_executable(HarmonyRender
    EXCLUDE_FROM_ALL
        tools/harmony_render.cpp
        src/HarmonyProcessor.cpp
        src/HarmonyEditor.cpp)
add_dependencies(HarmonyRender SattariHarmony)
target_include_directories(HarmonyRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariHarmony_artefacts/JuceLibraryCode)
target_compile_definitions(HarmonyRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(HarmonyRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
sattari_enable_rubberband(HarmonyRender SATTARI_HARMONY_HAS_RUBBERBAND)
