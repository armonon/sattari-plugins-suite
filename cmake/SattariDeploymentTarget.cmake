# =============================================================================
#  Sattari Audio - macOS deployment floor + architectures
#  (MUST be include()d BEFORE project())
# -----------------------------------------------------------------------------
#  Set the minimum macOS version before project() so the toolchain stamps the
#  right minimum-OS into the Mach-O. Without this, building on a newer macOS
#  (e.g. Tahoe / macOS 26) marks the binary as requiring that same OS, so it
#  crashes on load on every older Mac (newer-SDK symbols resolve to null ->
#  access violation in the host). 10.13 is JUCE 8's minimum. Applies to both
#  universal slices. Lesson harvested from the MK-ULTRA build.
#
#  Universal build: ship one binary that runs on Apple Silicon AND Intel Macs.
#  SATTARI_UNIVERSAL is ON by default; the floor (10.13) is honoured on the
#  x86_64 slice, while the arm64 slice is floored at 11.0 by the linker (arm64
#  macOS started at Big Sur), so a universal build proves the 10.13 floor too.
#  For fast local iteration, configure with -DSATTARI_UNIVERSAL=OFF (host arch
#  only) or pass an explicit -DCMAKE_OSX_ARCHITECTURES=... which is respected.
#
#  Usage (in every CMakeLists, between cmake_minimum_required and project()):
#     include(<path>/cmake/SattariDeploymentTarget.cmake)
# =============================================================================

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(SATTARI_UNIVERSAL "Build universal arm64 + x86_64 macOS binaries" ON)

if(APPLE AND SATTARI_UNIVERSAL AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures" FORCE)
endif()
