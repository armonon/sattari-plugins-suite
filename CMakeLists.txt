cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUDIO_PLUGINS VERSION 0.1.0)

add_subdirectory(core)

add_subdirectory(plugins/sub-conjurer)
add_subdirectory(plugins/stemdeck)
add_subdirectory(plugins/side-chain-master)
add_subdirectory(plugins/royal-chain)
add_subdirectory(plugins/harmony)
add_subdirectory(plugins/maqam)
add_subdirectory(plugins/vocal-master)
add_subdirectory(plugins/rhythm-genie)
add_subdirectory(plugins/sattari-arp)
add_subdirectory(plugins/auto-pitch)
add_subdirectory(plugins/song-key)
add_subdirectory(plugins/voxkey)
add_subdirectory(plugins/bus)
add_subdirectory(plugins/align)
add_subdirectory(plugins/warp)
add_subdirectory(plugins/tuner)
add_subdirectory(plugins/double)
add_subdirectory(plugins/deess)
add_subdirectory(plugins/face)
add_subdirectory(plugins/transient)
add_subdirectory(plugins/meter)
add_subdirectory(plugins/scaler)
add_subdirectory(plugins/eq)
add_subdirectory(plugins/space)
add_subdirectory(plugins/clean)
add_subdirectory(plugins/comp)
add_subdirectory(plugins/peak)
add_subdirectory(plugins/vocoder)
add_subdirectory(plugins/shimmer)
add_subdirectory(plugins/echo)
add_subdirectory(plugins/heat)
add_subdirectory(plugins/multiband)
add_subdirectory(plugins/gate)
add_subdirectory(plugins/riser)
add_subdirectory(plugins/pulse)
add_subdirectory(plugins/eightoheight)
add_subdirectory(plugins/imager)
add_subdirectory(plugins/warble)
add_subdirectory(plugins/scope)
add_subdirectory(plugins/granulator)
add_subdirectory(plugins/glass-choir)
add_subdirectory(plugins/brain)
add_subdirectory(plugins/voxsynth)
add_subdirectory(plugins/morph)

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
