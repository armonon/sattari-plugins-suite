cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MAQAM_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariMaqam
    SLUG maqam
    PRODUCT_NAME "Sattari Maqam"
    PLUGIN_CODE Mq01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/MaqamProcessor.cpp
        src/MaqamEditor.cpp)

sattari_enable_rubberband(SattariMaqam SATTARI_MAQAM_HAS_RUBBERBAND)

# Pure-C++ microtonal contract (no JUCE; mirrors the other core gates).
add_executable(MaqamCoreContract EXCLUDE_FROM_ALL tests/maqam_core_contract.cpp)
target_include_directories(MaqamCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)
