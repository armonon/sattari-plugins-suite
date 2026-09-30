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
        src/MacroController.cpp
        src/CymaticPlate.cpp
        src/PortalRenderer.cpp
        src/WaterDish.cpp
        src/CompassFeed.cpp
        src/RoyalChainPanels.cpp)

# The Tune module runs Auto Pitch's Extreme Live engine
# (sattari_audio_core/SattariLivePitchEngine.h, header-only, pure std), so Royal
# Chain no longer links Rubber Band.

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

# Headless UI snapshot + bench tool for the "Crown Portal" Compass and editor:
# renders PNGs at 1x/2x through the real CoreGraphics path, gates the visual
# metrics (brightness, coverage, blow-out, figure identity, gestures, layout,
# allocations) and, with --bench, measures thread CPU time per frame.
set(RC_UI_SOURCES src/RoyalChainProcessor.cpp src/RoyalChainEditor.cpp src/CleanModule.cpp src/TuneModule.cpp
    src/ControlModule.cpp src/ToneModule.cpp src/DeEsserModule.cpp src/ColorModule.cpp src/WidthModule.cpp
    src/SpaceModule.cpp src/FinishModule.cpp src/VocalCrownCompass.cpp src/MacroController.cpp
    src/CymaticPlate.cpp src/PortalRenderer.cpp src/WaterDish.cpp src/CompassFeed.cpp src/RoyalChainPanels.cpp)
add_executable(RoyalChainUIRender EXCLUDE_FROM_ALL tests/royal_chain_ui_render.cpp ${RC_UI_SOURCES})
add_dependencies(RoyalChainUIRender SattariRoyalChain)
target_include_directories(RoyalChainUIRender PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/SattariRoyalChain_artefacts/JuceLibraryCode)
target_compile_definitions(RoyalChainUIRender PUBLIC JucePlugin_Build_Standalone=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 "JucePlugin_Name=\"Sattari Royal Chain\"" SATTARI_ROYALCHAIN_HAS_RUBBERBAND=0)
target_link_libraries(RoyalChainUIRender PRIVATE SattariAudioCore juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tune stage offline render contract: detuned vocal-like phrases through the REAL
# processor (key C major, Tune = 1). Gates pitch pull-in (shared PitchDetector),
# reported-vs-measured latency, finite/non-silent output, zero allocations in
# processBlock, Tune-0 null against the delayed input and the Compass pitch feed.
add_executable(RoyalChainTuneRenderContract EXCLUDE_FROM_ALL tests/tune_render_contract.cpp ${RC_UI_SOURCES})
add_dependencies(RoyalChainTuneRenderContract SattariRoyalChain)
target_include_directories(RoyalChainTuneRenderContract PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/SattariRoyalChain_artefacts/JuceLibraryCode)
target_compile_definitions(RoyalChainTuneRenderContract PUBLIC JucePlugin_Build_Standalone=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 "JucePlugin_Name=\"Sattari Royal Chain\"")
target_link_libraries(RoyalChainTuneRenderContract PRIVATE SattariAudioCore juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
