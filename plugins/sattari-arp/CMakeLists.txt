cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_ARP_PLUGIN VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariArp
    SLUG sattari-arp
    PRODUCT_NAME "Sattari Arp"
    PLUGIN_CODE Arp1
    FORMATS AU VST3
    NEEDS_MIDI_INPUT
    NEEDS_MIDI_OUTPUT
    IS_MIDI_EFFECT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Sample-accurate step-timing contract (built on demand, like the other plugins' tests).
add_executable(SattariArpTimingContract
    EXCLUDE_FROM_ALL
        tests/sattari_arp_timing_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(SattariArpTimingContract SattariArp)

target_include_directories(SattariArpTimingContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariArp_artefacts/JuceLibraryCode)

target_compile_definitions(SattariArpTimingContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SattariArpTimingContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariReleaseUIContract.cmake)
sattari_add_release_ui_contract(SattariArp SattariArpAudioProcessor sattari-arp)
