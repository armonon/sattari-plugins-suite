cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_ROYAL_CHAIN_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)

sattari_add_plugin(
    TARGET SattariRoyalChain
    SLUG royal-chain
    PRODUCT_NAME "Sattari Royal Chain"
    PLUGIN_CODE Rc01
    SOURCES
        src/RoyalChainProcessor.cpp
        src/RoyalChainEditor.cpp
        src/CleanModule.cpp
        src/TuneModule.cpp
        src/ControlModule.cpp
        src/ToneModule.cpp
        src/DeEsserModule.cpp
        src/ColorModule.cpp
        src/WidthModule.cpp
        src/SpaceModule.cpp
        src/FinishModule.cpp
        src/VocalCrownCompass.cpp
        src/MacroController.cpp)

# Tune module shares the suite's Rubber Band realtime engine (GPL — see BUILD.md;
# a closed-source paid release needs the commercial licence, same as Auto Pitch).
sattari_enable_rubberband(SattariRoyalChain SATTARI_ROYALCHAIN_HAS_RUBBERBAND)

# Color anti-aliasing contract: ADAA tanh aliases less than naive per-sample tanh.
add_executable(RoyalChainColorAliasingContract
    EXCLUDE_FROM_ALL
        tests/color_aliasing_contract.cpp
        src/ColorModule.cpp)

add_dependencies(RoyalChainColorAliasingContract SattariRoyalChain)

target_include_directories(RoyalChainColorAliasingContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariRoyalChain_artefacts/JuceLibraryCode)

target_compile_definitions(RoyalChainColorAliasingContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(RoyalChainColorAliasingContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
