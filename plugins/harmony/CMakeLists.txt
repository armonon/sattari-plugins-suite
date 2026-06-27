cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_HARMONY_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariHarmony
    SLUG harmony
    PRODUCT_NAME "Sattari Harmony"
    PLUGIN_CODE Hm01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/HarmonyProcessor.cpp
        src/HarmonyEditor.cpp)

sattari_enable_rubberband(SattariHarmony SATTARI_HARMONY_HAS_RUBBERBAND)

# Pure-C++ diatonic-voicing contract (no JUCE; mirrors the other core gates).
add_executable(HarmonyCoreContract EXCLUDE_FROM_ALL tests/harmony_core_contract.cpp)
target_include_directories(HarmonyCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)
