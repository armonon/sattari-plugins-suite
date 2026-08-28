cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MULTIBAND_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariMultiband
    SLUG multiband
    PRODUCT_NAME "Sattari Multiband"
    PLUGIN_CODE Mb01
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Multiband contract: neutral (1:1, 0 dB thresholds) recombines ~flat pass-through,
# a hot band is compressed while a quiet band passes, bands recombine without large
# gain error, and output stays finite.
add_executable(MultibandContract
    EXCLUDE_FROM_ALL
        tests/multiband_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(MultibandContract SattariMultiband)

target_include_directories(MultibandContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariMultiband_artefacts/JuceLibraryCode)

target_compile_definitions(MultibandContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Sattari Multiband"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MultibandContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
