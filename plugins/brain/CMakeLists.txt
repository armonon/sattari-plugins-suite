cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_BRAIN_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariBrain
    SLUG brain
    PRODUCT_NAME "Sattari Brain"
    PLUGIN_CODE Br01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/BrainProcessor.cpp
        src/BrainEditor.cpp)

# Functional contract: transparent pass-through + full SessionState publish.
add_executable(BrainContract
    EXCLUDE_FROM_ALL
        tests/brain_contract.cpp
        src/BrainProcessor.cpp
        src/BrainEditor.cpp)
add_dependencies(BrainContract SattariBrain)

target_include_directories(BrainContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariBrain_artefacts/JuceLibraryCode)

target_compile_definitions(BrainContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BrainContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
