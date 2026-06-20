cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SUB_CONJURER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reference adopter of the shared Sattari packaging path (cmake/SattariPlugin.cmake).
# The other six plugins migrate to this same call; their CMakeLists collapse to it.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SubConjurer
    SLUG sub-conjurer
    PRODUCT_NAME "Sub Conjurer"
    PLUGIN_CODE Sc01
    NEEDS_MIDI_INPUT
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
