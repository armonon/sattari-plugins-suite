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
