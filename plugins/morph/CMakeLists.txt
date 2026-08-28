cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MORPH_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariMorph
    SLUG morph
    PRODUCT_NAME "Sattari Morph"
    PLUGIN_CODE Mph1
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
