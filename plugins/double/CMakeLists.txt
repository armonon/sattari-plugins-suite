cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_DOUBLE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariDouble
    SLUG double
    PRODUCT_NAME "Sattari Double"
    PLUGIN_CODE Db01
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Functional contract: proves the doubler widens (adds stereo/decorrelated
# energy) and that dry->wet actually changes the sound, staying finite + bounded.
add_executable(DoubleContract
    EXCLUDE_FROM_ALL
        tests/double_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(DoubleContract SattariDouble)

target_include_directories(DoubleContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariDouble_artefacts/JuceLibraryCode)

target_compile_definitions(DoubleContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DoubleContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
