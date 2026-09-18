cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_SONG_KEY_PLUGIN VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

set(SATTARI_SONGKEY_ARA_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ARA_SDK" CACHE PATH "ARA SDK for direct arrangement clip analysis")
set(_song_key_ara_default OFF)
if(EXISTS "${SATTARI_SONGKEY_ARA_SDK_PATH}/ARA_API/ARAInterface.h")
    set(_song_key_ara_default ON)
endif()
option(SATTARI_SONGKEY_ENABLE_ARA "Enable direct DAW clip access through ARA" ${_song_key_ara_default})
set(_song_key_ara_args)
set(_song_key_ara_sources)
if(SATTARI_SONGKEY_ENABLE_ARA)
    if(NOT EXISTS "${SATTARI_SONGKEY_ARA_SDK_PATH}/ARA_API/ARAInterface.h")
        message(FATAL_ERROR "Sattari Key ARA requested but the ARA SDK is missing")
    endif()
    list(APPEND _song_key_ara_args IS_ARA_EFFECT ARA_SDK_PATH "${SATTARI_SONGKEY_ARA_SDK_PATH}")
    list(APPEND _song_key_ara_sources src/SongKeyARA.cpp)
endif()

sattari_add_plugin(
    TARGET SattariSongKey
    SLUG song-key
    PRODUCT_NAME "Sattari Key"
    PLUGIN_CODE Sk01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    ${_song_key_ara_args}
    SOURCES
        src/SongKeyProcessor.cpp
        src/SongKeyEditor.cpp
        ${_song_key_ara_sources})

# The optional key model lost the only held-out evaluation. Production Sattari
# Key is deliberately model-free: no hidden model loading, 30-second ring, or
# ONNX runtime dependency. Research evaluators remain separate targets.
target_compile_definitions(SattariSongKey PRIVATE SATTARI_HAS_ONNX=0)

# Offline detection proof: feed a chord, assert the chroma->key chain detects it.
add_executable(SongKeyRender
    EXCLUDE_FROM_ALL
        tools/song_key_render.cpp
        src/SongKeyProcessor.cpp
        src/SongKeyEditor.cpp)
add_dependencies(SongKeyRender SattariSongKey)
target_include_directories(SongKeyRender
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariSongKey_artefacts/JuceLibraryCode)
target_compile_definitions(SongKeyRender
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SongKeyRender
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Same release shape for the render tool (it compiles the processor directly).
target_compile_definitions(SongKeyRender PRIVATE SATTARI_HAS_ONNX=0)

if(SATTARI_SONGKEY_ENABLE_ARA)
    # Link the actual production ARA factory without building/installing an AU
    # or VST3. This contract acts as a small in-process host using synthetic audio.
    add_executable(SongKeyARAContract EXCLUDE_FROM_ALL
        tests/arrangement_ara_contract.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../../third_party/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_ARA.cpp)
    target_include_directories(SongKeyARAContract PRIVATE $<TARGET_PROPERTY:SattariSongKey,INCLUDE_DIRECTORIES>)
    target_compile_definitions(SongKeyARAContract PRIVATE
        $<TARGET_PROPERTY:SattariSongKey,COMPILE_DEFINITIONS> JucePlugin_Build_Standalone=1 SATTARI_TEST_AUTOPITCH=0)
    target_link_libraries(SongKeyARAContract PRIVATE SattariSongKey)
endif()
