cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_STACK_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariStack
    SLUG stack
    PRODUCT_NAME "Sattari Stack"
    PLUGIN_CODE Stk1
    # IS_SYNTH makes this `aumu`. Unlike the effect racks this is not a choice that
    # could be deferred: an instrument and an effect are different AU types, a host
    # lists them in different places, and switching later would orphan every saved
    # session. Declared before a single instrument ships.
    IS_SYNTH
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_executable(SattariStackLayerContract
    EXCLUDE_FROM_ALL
        tests/stack_layer_contract.cpp)

target_compile_definitions(SattariStackLayerContract
    PUBLIC
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(SattariStackLayerContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
