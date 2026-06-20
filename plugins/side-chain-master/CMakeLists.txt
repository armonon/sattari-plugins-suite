cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SIDE_CHAIN_MASTER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SideChainMaster
    SLUG side-chain-master
    PRODUCT_NAME "Side Chain Master"
    PLUGIN_CODE Scm1
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
