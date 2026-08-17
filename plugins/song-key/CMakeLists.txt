cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_SONG_KEY_PLUGIN VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SattariSongKey
    SLUG song-key
    PRODUCT_NAME "Sattari Key"
    PLUGIN_CODE Sk01
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/SongKeyProcessor.cpp
        src/SongKeyEditor.cpp)

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
