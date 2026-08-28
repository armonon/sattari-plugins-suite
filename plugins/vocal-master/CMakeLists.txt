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

# Offline render + "clean the vocal out of the mix" proof (links processor + JUCE).
add_executable(VocalMasterRender
    EXCLUDE_FROM_ALL
        tools/vocal_master_render.cpp
        src/VocalMasterProcessor.cpp
        src/VocalMasterEditor.cpp)
add_dependencies(VocalMasterRender SattariVocalMaster)
target_include_directories(VocalMasterRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariVocalMaster_artefacts/JuceLibraryCode)
target_compile_definitions(VocalMasterRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(VocalMasterRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
sattari_enable_onnx(VocalMasterRender SATTARI_HAS_ONNX)
