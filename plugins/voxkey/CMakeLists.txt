cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_VOXKEY_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

set(VOXKEY_CORE_SOURCES
    src/PitchDetector.cpp
    src/VoiceMidiMapper.cpp)

sattari_add_plugin(
    TARGET SattariVoxKey
    SLUG voxkey
    PRODUCT_NAME "Sattari VoxKey"
    PLUGIN_CODE Vk01
    NEEDS_MIDI_OUTPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        ${VOXKEY_CORE_SOURCES}
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Functional DSP contract test (feeds a 440 Hz sine, asserts YIN -> A4).
add_executable(VoxKeyCoreContract
    tests/voxkey_core_contract.cpp
    ${VOXKEY_CORE_SOURCES})

target_include_directories(VoxKeyCoreContract PRIVATE src)

target_compile_definitions(VoxKeyCoreContract
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(VoxKeyCoreContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
