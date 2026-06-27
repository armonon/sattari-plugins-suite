cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_RHYTHM_GENIE_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariRhythmGenie
    SLUG rhythm-genie
    PRODUCT_NAME "Sattari Rhythm Genie"
    PLUGIN_CODE Rg01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    NEEDS_MIDI_OUTPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/RhythmGenieProcessor.cpp
        src/RhythmGenieEditor.cpp)

# Pure-C++ pattern contract (no JUCE; mirrors the other core gates).
add_executable(RhythmGenieCoreContract EXCLUDE_FROM_ALL tests/rhythm_genie_core_contract.cpp)
target_include_directories(RhythmGenieCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)
