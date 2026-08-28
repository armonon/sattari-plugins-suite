cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_808_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET Sattari808
    SLUG eightoheight
    PRODUCT_NAME "Sattari 808"
    PLUGIN_CODE S808
    NEEDS_MIDI_INPUT
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Functional contract: proves blend=0 is dry pass-through, a steady low tone
# generates a tuned sub whose fundamental matches the input note, the output
# stays bounded, and no sample is ever non-finite.
add_executable(Sattari808Contract
    EXCLUDE_FROM_ALL
        tests/eightoheight_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(Sattari808Contract Sattari808)

target_include_directories(Sattari808Contract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/Sattari808_artefacts/JuceLibraryCode)

target_compile_definitions(Sattari808Contract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sattari 808"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Sattari808Contract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
