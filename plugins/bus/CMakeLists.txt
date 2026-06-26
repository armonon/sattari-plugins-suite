cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_BUS_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Built entirely on the shared Sattari packaging path.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariBus
    SLUG bus
    PRODUCT_NAME "Sattari Bus"
    PLUGIN_CODE Bus1
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Core DSP contract: width-collapse, M/S round-trip, mute.
add_executable(SattariBusCoreContract
    EXCLUDE_FROM_ALL
        tests/bus_core_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(SattariBusCoreContract SattariBus)

target_include_directories(SattariBusCoreContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariBus_artefacts/JuceLibraryCode)

target_compile_definitions(SattariBusCoreContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sattari Bus"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SattariBusCoreContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
