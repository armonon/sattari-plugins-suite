# =============================================================================
#  Sattari Audio - vendored Rubber Band time/pitch engine
# -----------------------------------------------------------------------------
#  Builds Rubber Band from source via its single-file compilation unit
#  (third_party/rubberband/single/RubberBandSingle.cpp) - no Homebrew, no
#  pkg-config, reproducible on any machine and in CI. This replaces the old
#  "optional local dylib if you happen to have it" path.
#
#  The single unit self-configures: built-in BQ resampler, vDSP FFT on Apple
#  (so the Accelerate framework is linked there), built-in FFT elsewhere, and
#  single-threaded (NO_THREADING) which suits per-channel realtime use.
#
#  Usage (per plugin target that wants time/pitch):
#     include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
#     sattari_enable_rubberband(MyTarget MY_HAS_RUBBERBAND_DEFINE)
#  -> links the engine and sets MY_HAS_RUBBERBAND_DEFINE=1, or =0 (with a
#     warning) if the submodule isn't checked out yet, so the tree still builds.
#
#  LICENSE: Rubber Band is GPLv2-or-commercial. Vendoring/linking it is fine for
#  GPL or internal builds; a closed-source PAID release needs the commercial
#  Rubber Band licence. See third_party/rubberband/COPYING and
#  docs/sattari-platform-roadmap.md.
# =============================================================================

include_guard(GLOBAL)

option(SATTARI_USE_RUBBERBAND "Build + link the vendored Rubber Band engine" ON)

function(sattari_enable_rubberband target define_name)
    if(NOT SATTARI_USE_RUBBERBAND)
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    # Resolve the repo root from the dir of the file that DEFINED this function
    # (<root>/cmake), not from the caller's scope. include_guard(GLOBAL) means a
    # file-scope variable set on first include is absent in later includers'
    # directory scopes, which silently disabled Rubber Band for the second
    # consumer (auto-pitch). CMAKE_CURRENT_FUNCTION_LIST_DIR is scope-independent.
    get_filename_component(_rb_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(rb "${_rb_root}/third_party/rubberband")
    set(single "${rb}/single/RubberBandSingle.cpp")

    if(NOT EXISTS "${single}")
        message(WARNING
            "Rubber Band not vendored at ${rb} "
            "(run: git submodule update --init --recursive). "
            "Building ${target} with ${define_name}=0 (fallback engine).")
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    if(NOT TARGET sattari_rubberband)
        add_library(sattari_rubberband STATIC "${single}")
        target_include_directories(sattari_rubberband PUBLIC "${rb}")
        target_compile_features(sattari_rubberband PUBLIC cxx_std_17)
        set_target_properties(sattari_rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(APPLE)
            target_link_libraries(sattari_rubberband PUBLIC "-framework Accelerate")
        endif()
        if(WIN32)
            target_compile_definitions(sattari_rubberband PRIVATE NOMINMAX)
        endif()
        if(MSVC)
            target_compile_definitions(sattari_rubberband PRIVATE _USE_MATH_DEFINES)
        endif()
    endif()

    target_link_libraries(${target} PRIVATE sattari_rubberband)
    target_compile_definitions(${target} PRIVATE ${define_name}=1)
endfunction()
