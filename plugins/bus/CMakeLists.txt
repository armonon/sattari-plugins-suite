cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_BUS_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Built entirely on the shared Sattari packaging path.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariBus
    SLUG bus
    PRODUCT_NAME "Sattari Bus"
    PLUGIN_CODE Bus1
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
