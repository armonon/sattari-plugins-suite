cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_CREATE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariCreate
    SLUG create
    PRODUCT_NAME "Sattari Create"
    PLUGIN_CODE Cr01
    # Same reasoning as Sattari Mix: MIDI input makes this an `aumf` music effect
    # rather than a plain `aufx`, and the AU type is part of the plugin's identity,
    # so it is declared UP FRONT rather than switched later. Create's roster is
    # headed for key/tempo-aware modules (Shimmer's key snap today, Vocoder and
    # 808 later), none of which could be added if the type were fixed as aufx.
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
