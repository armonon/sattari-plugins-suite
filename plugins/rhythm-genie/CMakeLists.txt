cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_RHYTHM_GENIE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariRhythmGenie
    SLUG rhythm-genie
    PRODUCT_NAME "Sattari Rhythm Genie"
    PLUGIN_CODE Rg01
    VERSION ${PROJECT_VERSION}
    IS_SYNTH
    NEEDS_MIDI_INPUT
    NEEDS_MIDI_OUTPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/RhythmGenieProcessor.cpp
        src/RhythmGenieEditor.cpp)

# Pure-C++ pattern contract (no JUCE; mirrors the other core gates).
add_executable(RhythmGenieCoreContract EXCLUDE_FROM_ALL tests/rhythm_genie_core_contract.cpp)
target_include_directories(RhythmGenieCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)

# Offline MIDI-generation proof: drives a mock playhead and checks the grid timing.
add_executable(RhythmGenieRender
    EXCLUDE_FROM_ALL
        tools/rhythm_genie_render.cpp
        src/RhythmGenieProcessor.cpp
        src/RhythmGenieEditor.cpp)
add_dependencies(RhythmGenieRender SattariRhythmGenie)
target_include_directories(RhythmGenieRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariRhythmGenie_artefacts/JuceLibraryCode)
target_compile_definitions(RhythmGenieRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RhythmGenieRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
