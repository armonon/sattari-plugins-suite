# =============================================================================
#  Sattari Audio - zplane elastique v3 pitch-shifting engine
# -----------------------------------------------------------------------------
#  elastique v3 is a commercial SDK from zplane. Drop the SDK into
#  third_party/elastique/ so the directory contains:
#    third_party/elastique/elastique3/IElastiqueV3.h
#    third_party/elastique/lib/macos/libelastiquepro.a   (or .dylib)
#    third_party/elastique/lib/win/elastiquepro.lib
#
#  License: purchase at zplane.de. One dev-seat license covers internal
#  builds; a distribution license is required before shipping.
#
#  Usage (per plugin target):
#    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariElastique.cmake)
#    sattari_enable_elastique(MyTarget MY_HAS_ELASTIQUE_DEFINE)
#  -> links the engine, sets define=1, or =0 (with a warning) when the
#     SDK is absent so the tree still builds with the RubberBand fallback.
# =============================================================================

include_guard(GLOBAL)

option(SATTARI_USE_ELASTIQUE "Link the zplane elastique v3 engine (requires SDK)" ON)

function(sattari_enable_elastique target define_name)
    if(NOT SATTARI_USE_ELASTIQUE)
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    get_filename_component(_eq_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(eq_dir "${_eq_root}/third_party/elastique")
    set(eq_header "${eq_dir}/elastique3/IElastiqueV3.h")

    if(NOT EXISTS "${eq_header}")
        message(WARNING
            "zplane elastique SDK not found at ${eq_dir} "
            "(place the SDK there — see cmake/SattariElastique.cmake). "
            "Building ${target} with ${define_name}=0 (RubberBand fallback).")
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    if(NOT TARGET sattari_elastique)
        add_library(sattari_elastique INTERFACE)
        target_include_directories(sattari_elastique INTERFACE "${eq_dir}")

        if(APPLE)
            find_library(_eq_lib elastiquepro
                PATHS "${eq_dir}/lib/macos"
                NO_DEFAULT_PATH)
            if(NOT _eq_lib)
                # Try dylib variant
                find_library(_eq_lib elastiquepro
                    PATHS "${eq_dir}/lib/macos"
                    NO_DEFAULT_PATH
                    SUFFIXES ".dylib")
            endif()
        elseif(WIN32)
            find_library(_eq_lib elastiquepro
                PATHS "${eq_dir}/lib/win"
                NO_DEFAULT_PATH)
        endif()

        if(NOT _eq_lib)
            message(WARNING
                "elastique header found but library not found under ${eq_dir}/lib. "
                "Building ${target} with ${define_name}=0.")
            target_compile_definitions(${target} PRIVATE ${define_name}=0)
            return()
        endif()

        target_link_libraries(sattari_elastique INTERFACE "${_eq_lib}")
        if(APPLE)
            target_link_libraries(sattari_elastique INTERFACE "-framework Accelerate")
        endif()
    endif()

    target_link_libraries(${target} PRIVATE sattari_elastique)
    target_compile_definitions(${target} PRIVATE ${define_name}=1)
endfunction()
