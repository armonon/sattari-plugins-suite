cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_TUNER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariTuner
    SLUG tuner
    PRODUCT_NAME "Sattari Tuner"
    PLUGIN_CODE Tn01
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Functional contract: proves the tuner reports the correct note + near-zero
# cents on a known reference tone, and passes audio through untouched.
add_executable(TunerContract
    EXCLUDE_FROM_ALL
        tests/tuner_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(TunerContract SattariTuner)

target_include_directories(TunerContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariTuner_artefacts/JuceLibraryCode)

target_compile_definitions(TunerContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TunerContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
