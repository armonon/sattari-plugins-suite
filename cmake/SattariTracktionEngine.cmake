# =============================================================================
#  Sattari Audio - Tracktion Engine integration
# -----------------------------------------------------------------------------
#  Adds the tracktion_engine JUCE module from third_party/tracktion_engine.
#  We deliberately skip TE's own JUCE submodule and use the repo-level JUCE
#  that is already configured by the root CMakeLists, so there is exactly one
#  JUCE on the link graph.
#
#  Usage (per plugin target):
#     include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariTracktionEngine.cmake)
#     sattari_enable_tracktion_engine(MyTarget)
# =============================================================================

include_guard(GLOBAL)

get_filename_component(SATTARI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(TE_ROOT "${SATTARI_REPO_ROOT}/third_party/tracktion_engine")

function(sattari_enable_tracktion_engine TARGET)
    if(NOT EXISTS "${TE_ROOT}/modules/tracktion_engine/tracktion_engine.h")
        message(WARNING
            "sattari_enable_tracktion_engine: Tracktion Engine submodule not found at "
            "${TE_ROOT}. Run: git submodule update --init third_party/tracktion_engine")
        return()
    endif()

    # Tell TE where to find JUCE modules (already in our tree).
    # We add TE's module dirs directly to JUCE's known path so
    # juce_add_module() can resolve them.
    juce_add_module("${TE_ROOT}/modules/tracktion_graph"   ALIAS_NAMESPACE tracktion)
    juce_add_module("${TE_ROOT}/modules/tracktion_core"    ALIAS_NAMESPACE tracktion)
    juce_add_module("${TE_ROOT}/modules/tracktion_engine"  ALIAS_NAMESPACE tracktion)

    target_link_libraries(${TARGET}
        PRIVATE
            tracktion::tracktion_graph
            tracktion::tracktion_core
            tracktion::tracktion_engine)

    # TE requires C++20
    set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
    set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD_REQUIRED ON)

    # TE expects the engine to own the APVTS — suppress double-init warnings.
    target_compile_definitions(${TARGET} PUBLIC TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0)

    message(STATUS "Tracktion Engine enabled for ${TARGET} (v3.2.0, C++20)")
endfunction()
