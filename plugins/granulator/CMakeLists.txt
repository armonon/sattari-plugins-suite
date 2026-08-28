cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_GRANULATOR_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariGranulator
    SLUG granulator
    PRODUCT_NAME "Sattari Granulator"
    PLUGIN_CODE Gr01
    IS_SYNTH
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/WaveformView.cpp)

add_executable(GranulatorContract
    EXCLUDE_FROM_ALL
        tests/granulator_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/WaveformView.cpp)
add_dependencies(GranulatorContract SattariGranulator)

target_include_directories(GranulatorContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariGranulator_artefacts/JuceLibraryCode)

target_compile_definitions(GranulatorContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GranulatorContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
