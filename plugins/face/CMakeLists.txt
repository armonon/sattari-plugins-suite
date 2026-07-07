cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_FACE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariFace
    SLUG face
    PRODUCT_NAME "Sattari Face"
    PLUGIN_CODE Fc01
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
sattari_enable_rubberband(SattariFace SATTARI_FACE_HAS_RUBBERBAND)

# Functional contract: proves pitch shift moves the fundamental while formant
# shift changes the timbre, and everything stays finite + non-silent.
add_executable(FaceContract
    EXCLUDE_FROM_ALL
        tests/face_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(FaceContract SattariFace)
sattari_enable_rubberband(FaceContract SATTARI_FACE_HAS_RUBBERBAND)

target_include_directories(FaceContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariFace_artefacts/JuceLibraryCode)

target_compile_definitions(FaceContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FaceContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
