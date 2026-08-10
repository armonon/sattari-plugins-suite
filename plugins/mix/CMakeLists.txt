cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MIX_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariMix
    SLUG mix
    PRODUCT_NAME "Sattari Mix"
    PLUGIN_CODE Mx01
    # MIDI input makes this an `aumf` music effect rather than a plain `aufx`. It is
    # declared UP FRONT, before the rack ships, because the AU type is part of the
    # plugin's identity: switching later makes hosts treat it as a different plugin and
    # silently drop it from saved sessions. 14 plugins in the suite need MIDI input
    # (Vocoder's synth carrier, 808, Sub Conjurer, Harmony, Maqam ...), and none of them
    # could ever become a rack module without this. Costs nothing for audio-only modules.
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_executable(SattariMixProductContract EXCLUDE_FROM_ALL
    tests/mix_product_contract.cpp)
target_include_directories(SattariMixProductContract PRIVATE
    src
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include
    ${CMAKE_CURRENT_LIST_DIR}/../../core/tests
    ${CMAKE_CURRENT_BINARY_DIR}/SattariMix_artefacts/JuceLibraryCode)
target_link_libraries(SattariMixProductContract PRIVATE
    SattariMix
    juce::juce_audio_utils
    juce::juce_dsp)
