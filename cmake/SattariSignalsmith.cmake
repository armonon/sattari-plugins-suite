# =============================================================================
#  Sattari Audio - signalsmith-stretch pitch/time engine (MIT)
# -----------------------------------------------------------------------------
#  signalsmith-stretch is a header-only C++11 library. It uses FetchContent
#  internally to pull signalsmith-linear; internet access is required on the
#  first configure if the linear submodule isn't already present.
#
#  The library lives at third_party/signalsmith-stretch/ (git submodule).
#
#  Usage (per plugin target):
#    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariSignalsmith.cmake)
#    sattari_enable_signalsmith(MyTarget MY_HAS_SIGNALSMITH_DEFINE)
#  -> sets define=1 and links, or =0 with a warning when the submodule is
#     absent, so the tree falls through to RubberBand.
#
#  Engine priority in the plugin: elastique > signalsmith > RubberBand > fallback
#  License: MIT — free for commercial use, no royalties.
# =============================================================================

include_guard(GLOBAL)

option(SATTARI_USE_SIGNALSMITH "Build + link the vendored signalsmith-stretch engine" ON)

function(sattari_enable_signalsmith target define_name)
    if(NOT SATTARI_USE_SIGNALSMITH)
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    get_filename_component(_ss_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(ss_dir "${_ss_root}/third_party/signalsmith-stretch")
    set(ss_header "${ss_dir}/signalsmith-stretch.h")

    if(NOT EXISTS "${ss_header}")
        message(WARNING
            "signalsmith-stretch not found at ${ss_dir} "
            "(run: git submodule update --init --recursive). "
            "Building ${target} with ${define_name}=0 (RubberBand fallback).")
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    if(NOT TARGET signalsmith-stretch)
        # Disable signalsmith-linear's Accelerate backend: it would propagate
        # SIGNALSMITH_USE_ACCELERATE + ACCELERATE_NEW_LAPACK as INTERFACE compile
        # definitions, pulling in <vecLib/vecLib.h> -> <MacTypes.h> -> C "Point" typedef
        # into every JUCE .cpp/.mm TU that includes PluginProcessor.h, which conflicts
        # with juce::Point when using namespace juce is active. JUCE uses Accelerate
        # directly for its own DSP, so we lose nothing by skipping it here.
        set(SIGNALSMITH_USE_ACCELERATE OFF CACHE BOOL "" FORCE)
        add_subdirectory("${ss_dir}" "${CMAKE_BINARY_DIR}/signalsmith-stretch" EXCLUDE_FROM_ALL)
    endif()

    target_link_libraries(${target} PRIVATE signalsmith-stretch)
    target_compile_definitions(${target} PRIVATE ${define_name}=1)
endfunction()
