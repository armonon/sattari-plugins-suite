cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SIDE_CHAIN_MASTER_PLUGIN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)

sattari_add_plugin(
    TARGET SideChainMaster
    SLUG side-chain-master
    PRODUCT_NAME "Side Chain Master"
    PLUGIN_CODE Scm1
    SOURCES
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

# Ducking contract: external sidechain attenuates the main output.
add_executable(SideChainMasterCoreContract
    EXCLUDE_FROM_ALL
        tests/side_chain_core_contract.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

add_dependencies(SideChainMasterCoreContract SideChainMaster)

target_include_directories(SideChainMasterCoreContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SideChainMaster_artefacts/JuceLibraryCode)

target_compile_definitions(SideChainMasterCoreContract
    PUBLIC
        JucePlugin_Build_Standalone=1
        JucePlugin_Name="Side Chain Master"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SideChainMasterCoreContract
    PRIVATE
        SattariAudioCore
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
