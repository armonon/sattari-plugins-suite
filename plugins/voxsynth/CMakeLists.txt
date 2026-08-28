cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_VOXSYNTH_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariVoxSynth
    SLUG voxsynth
    PRODUCT_NAME "Sattari VoxSynth"
    PLUGIN_CODE Vsy1
    IS_SYNTH
    NEEDS_MIDI_OUTPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# MPE output contract: the bend range must be ANNOUNCED (RPN 6 then RPN 0) before
# the first bend, and the bendRange parameter must actually scale the bends. Sending
# 48-semitone-scaled bends to an instrument still on the 2-semitone default made every
# vibrato 24x too wide, which looked like a detector fault rather than a MIDI one.
add_executable(VoxSynthMpeContract
    EXCLUDE_FROM_ALL
        tests/voxsynth_mpe_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
add_dependencies(VoxSynthMpeContract SattariVoxSynth)

target_include_directories(VoxSynthMpeContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariVoxSynth_artefacts/JuceLibraryCode)

target_compile_definitions(VoxSynthMpeContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VoxSynthMpeContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
