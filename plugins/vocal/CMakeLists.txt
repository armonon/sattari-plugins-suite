cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_VOCAL_PLUGIN VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariVocal
    SLUG vocal
    PRODUCT_NAME "Sattari Vocal"
    PLUGIN_CODE Vcl1
    # Same reasoning as Mix and Create: AU type is plugin identity, so MIDI input
    # is declared up front rather than switched after release. The Vocal roster is
    # headed for key- and MIDI-aware modules (Harmony voices, Vocoder).
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Face's shifter is Rubber Band-backed, so the host needs the engine and the
# shared guard. Without the submodule the guard is 0 and Face degrades to a dry
# copy rather than failing the build.
sattari_enable_rubberband(SattariVocal SATTARI_HAS_RUBBERBAND)

add_executable(SattariVocalProductContract EXCLUDE_FROM_ALL
    tests/vocal_product_contract.cpp)
target_include_directories(SattariVocalProductContract PRIVATE
    src
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/tests
    ${CMAKE_CURRENT_BINARY_DIR}/SattariVocal_artefacts/JuceLibraryCode)
target_link_libraries(SattariVocalProductContract PRIVATE
    SattariVocal
    juce::juce_audio_utils
    juce::juce_dsp)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariReleaseUIContract.cmake)
sattari_add_release_ui_contract(SattariVocal SattariVocalAudioProcessor vocal)
