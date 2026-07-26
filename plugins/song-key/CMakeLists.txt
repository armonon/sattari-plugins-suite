cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_SONG_KEY_PLUGIN VERSION 0.1.0)

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

# The spectrogram key model (keynet_spec.onnx, 72.1 MIREX) runs on the worker thread.
# Optional: without ONNX the KeyNetSpec wrapper compiles to an inert stub and the
# plugin falls back to its HPCP chroma detector.
sattari_enable_onnx(SattariSongKey SATTARI_HAS_ONNX)

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

# Same for the render tool (it compiles SongKeyProcessor.cpp directly).
sattari_enable_onnx(SongKeyRender SATTARI_HAS_ONNX)
