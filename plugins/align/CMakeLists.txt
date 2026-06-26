cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_ALIGN_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariAlign
    SLUG align
    PRODUCT_NAME "Sattari Align"
    PLUGIN_CODE Algn
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Alignment contract: detects guide activity + offset via cross-correlation.
add_executable(SattariAlignCoreContract
    EXCLUDE_FROM_ALL
        tests/align_core_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(SattariAlignCoreContract SattariAlign)

target_include_directories(SattariAlignCoreContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariAlign_artefacts/JuceLibraryCode)

target_compile_definitions(SattariAlignCoreContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sattari Align"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SattariAlignCoreContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
