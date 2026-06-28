cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_VOCAL_MASTER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)

sattari_add_plugin(
    TARGET SattariVocalMaster
    SLUG vocal-master
    PRODUCT_NAME "Sattari Vocal Master"
    PLUGIN_CODE Vm01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/VocalMasterProcessor.cpp
        src/VocalMasterEditor.cpp)

# Optional AI stem removal (off by default; -DSATTARI_USE_ONNX=ON to enable the
# offline vocal-isolation path via SattariStemSeparator).
sattari_enable_onnx(SattariVocalMaster SATTARI_HAS_ONNX)

# Pure-C++ DSP contract (no JUCE; mirrors the other core gates).
add_executable(VocalMasterCoreContract EXCLUDE_FROM_ALL tests/vocal_master_core_contract.cpp)
target_include_directories(VocalMasterCoreContract
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../core/include
        ${CMAKE_CURRENT_LIST_DIR}/src)
