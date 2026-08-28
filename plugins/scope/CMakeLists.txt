cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_SCOPE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariScope
    SLUG scope
    PRODUCT_NAME "Sattari Scope"
    PLUGIN_CODE Scp1
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_executable(ScopeContract
    EXCLUDE_FROM_ALL
        tests/scope_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(ScopeContract SattariScope)

target_include_directories(ScopeContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariScope_artefacts/JuceLibraryCode)

target_compile_definitions(ScopeContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ScopeContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
