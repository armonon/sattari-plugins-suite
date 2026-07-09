cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_COMP_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariComp
    SLUG comp
    PRODUCT_NAME "Sattari Comp"
    PLUGIN_CODE Cmp1
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Compression contract: below-threshold passes ~unchanged, hot signal is reduced,
# mix=0 is pass-through, output stays finite.
add_executable(CompContract
    EXCLUDE_FROM_ALL
        tests/comp_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(CompContract SattariComp)

target_include_directories(CompContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariComp_artefacts/JuceLibraryCode)

target_compile_definitions(CompContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sattari Comp"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(CompContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
