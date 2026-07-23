cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_CHOP_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

# Sattari Chop — a slicing sampler (VST3/AU/Standalone), also embeddable in StemDeck.
sattari_add_plugin(
    TARGET SattariChop
    SLUG chop
    PRODUCT_NAME "Sattari Chop"
    PLUGIN_CODE Cho1
    VERSION ${PROJECT_VERSION}
    IS_SYNTH
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/ChopProcessor.cpp
        src/ChopEditor.cpp)

# Real-material proof: runs the onset detector over full-length GiantSteps tracks.
# The pure-std contracts (core/tests/{slice_table,onset_curve}_contract.cpp) prove the
# algorithms; this proves they survive dense mixes, which is what actually matters.
# Local-only (needs the validation corpus), so EXCLUDE_FROM_ALL.
add_executable(ChopOnsetProbe EXCLUDE_FROM_ALL tools/chop_onset_probe.cpp)
add_dependencies(ChopOnsetProbe SattariChop)
target_include_directories(ChopOnsetProbe
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariChop_artefacts/JuceLibraryCode)
target_compile_definitions(ChopOnsetProbe
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ChopOnsetProbe
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
