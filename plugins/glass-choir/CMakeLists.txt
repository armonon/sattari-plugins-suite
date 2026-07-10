cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_GLASS_CHOIR_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariGlassChoir
    SLUG glass-choir
    PRODUCT_NAME "Sattari Glass Choir"
    PLUGIN_CODE Gc01
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
sattari_enable_rubberband(SattariGlassChoir GLASSCHOIR_HAS_RUBBERBAND)

# Functional contract: proves the 3-bus ensemble render (Lead/Choir/Atmosphere),
# the single-out fold-down fallback, the character macro bounded across 3 modes,
# mono-safe width, ducking, and honest latency. Mirrors double/granulator harness.
add_executable(GlassChoirContract
    EXCLUDE_FROM_ALL
        tests/glasschoir_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(GlassChoirContract SattariGlassChoir)
sattari_enable_rubberband(GlassChoirContract GLASSCHOIR_HAS_RUBBERBAND)

target_include_directories(GlassChoirContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariGlassChoir_artefacts/JuceLibraryCode)

target_compile_definitions(GlassChoirContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GlassChoirContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
