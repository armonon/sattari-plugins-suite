cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUDIO_PLUGINS VERSION 0.1.0)

add_subdirectory(core)

add_subdirectory(plugins/sub-conjurer)
add_subdirectory(plugins/stemdeck)
add_subdirectory(plugins/side-chain-master)
add_subdirectory(plugins/royal-chain)
add_subdirectory(plugins/sattari-arp)
add_subdirectory(plugins/auto-pitch)
add_subdirectory(plugins/voxkey)
add_subdirectory(plugins/bus)
add_subdirectory(plugins/align)

# Cross-plugin session bus contract: defined here (not in core/) because it needs
# juce_core, which only exists after the plugins above have pulled JUCE in.
if(TARGET juce::juce_core)
    add_executable(SattariCrossPluginBusContract
        EXCLUDE_FROM_ALL
        core/tests/cross_plugin_bus_contract.cpp)
    target_link_libraries(SattariCrossPluginBusContract
        PRIVATE
            SattariAudioCore
            juce::juce_core
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
