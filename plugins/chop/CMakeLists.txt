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

# (ChopCoreContract — the slicing-engine contract — is added with the SliceTable +
#  OnsetCurve cores in the next commit.)
