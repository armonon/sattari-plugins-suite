cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_CLEAN_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariClean
    SLUG clean
    PRODUCT_NAME "Sattari Clean"
    PLUGIN_CODE Cl01
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_executable(CleanContract
    EXCLUDE_FROM_ALL
        tests/clean_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(CleanContract SattariClean)

target_include_directories(CleanContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariClean_artefacts/JuceLibraryCode)

target_compile_definitions(CleanContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(CleanContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
