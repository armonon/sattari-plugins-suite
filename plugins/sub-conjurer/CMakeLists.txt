cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SUB_CONJURER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reference adopter of the shared Sattari packaging path (cmake/SattariPlugin.cmake).
# The other six plugins migrate to this same call; their CMakeLists collapse to it.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SubConjurer
    SLUG sub-conjurer
    PRODUCT_NAME "Sub Conjurer"
    PLUGIN_CODE Sc01
    NEEDS_MIDI_INPUT
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Functional contract: proves the synth tracks a low tone into a bounded,
# non-silent sub-band signal, and stays quiet on silence (gate closes).
add_executable(SubConjurerContract
    EXCLUDE_FROM_ALL
        tests/sub_conjurer_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(SubConjurerContract SubConjurer)

target_include_directories(SubConjurerContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SubConjurer_artefacts/JuceLibraryCode)

target_compile_definitions(SubConjurerContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sub Conjurer"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SubConjurerContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
