# =============================================================================
#  Sattari Audio — optional ONNX Runtime (ML) integration
# -----------------------------------------------------------------------------
#  Mirrors cmake/SattariRubberBand.cmake: one call wires ONNX Runtime into a
#  target for the offline ML features (stem separation, CREPE-class pitch), and
#  is OFF by default so the suite always builds with no new dependency.
#
#     include(.../SattariOnnx.cmake)
#     sattari_enable_onnx(MyTarget MY_HAS_ONNX_DEFINE)
#  -> sets MY_HAS_ONNX_DEFINE=1 and links ONNX Runtime when found, else =0.
#
#  Enable with -DSATTARI_USE_ONNX=ON and point ONNXRUNTIME_ROOT at an extracted
#  onnxruntime release (https://github.com/microsoft/onnxruntime/releases).
# =============================================================================

include_guard(GLOBAL)

option(SATTARI_USE_ONNX "Enable ONNX Runtime ML features (stem separation, ML pitch)" OFF)


# Compare a dylib's LC_BUILD_VERSION minos against CMAKE_OSX_DEPLOYMENT_TARGET.
# Set SATTARI_ALLOW_ONNX_FLOOR_MISMATCH=ON to build anyway (local dev on a machine
# that meets the higher floor) — it stays loud so it can never pass unnoticed.
function(sattari_onnx_check_deployment_floor lib_path target)
    if(NOT APPLE OR NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        return()
    endif()

    execute_process(COMMAND otool -l "${lib_path}"
                    OUTPUT_VARIABLE _otool_out ERROR_QUIET)
    string(REGEX MATCH "minos[ \t]+([0-9]+(\.[0-9]+)*)" _m "${_otool_out}")
    if(NOT _m)
        message(STATUS "Sattari ONNX: could not read minos from ${lib_path}; floor unchecked")
        return()
    endif()
    set(_lib_min "${CMAKE_MATCH_1}")

    if(_lib_min VERSION_GREATER CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_msg
            "ONNX Runtime requires macOS ${_lib_min}, but this build declares a minimum of "
            "${CMAKE_OSX_DEPLOYMENT_TARGET} (target: ${target}).\n"
            "  Linking it produces a binary that claims ${CMAKE_OSX_DEPLOYMENT_TARGET} and "
            "FAILS TO LOAD on anything below ${_lib_min}.\n"
            "  lib: ${lib_path}\n"
            "  Fix by one of:\n"
            "    * build/vendor an onnxruntime with a lower deployment target and point "
            "ONNXRUNTIME_ROOT at it (recommended for shipping)\n"
            "    * configure with -DSATTARI_USE_ONNX=OFF (neural features compile out)\n"
            "    * raise -DCMAKE_OSX_DEPLOYMENT_TARGET=${_lib_min} and accept that floor\n"
            "    * -DSATTARI_ALLOW_ONNX_FLOOR_MISMATCH=ON for a local-only build")
        # Severity follows the existing convention in SattariDeploymentTarget.cmake:
        # SATTARI_UNIVERSAL=ON is the SHIPPING config (arm64 + x86_64), OFF is
        # documented as "fast local iteration". A mismatch is fatal for a shipping
        # build and a loud warning for a local one, so this cannot quietly reach a
        # release while still not blocking day-to-day work on a machine that already
        # meets the higher floor.
        if(SATTARI_ALLOW_ONNX_FLOOR_MISMATCH OR NOT SATTARI_UNIVERSAL)
            message(WARNING ${_msg})
        else()
            message(FATAL_ERROR ${_msg})
        endif()
    else()
        message(STATUS "Sattari ONNX: deployment floor ok (lib minos ${_lib_min} "
                       "<= target ${CMAKE_OSX_DEPLOYMENT_TARGET})")
    endif()
endfunction()

option(SATTARI_ALLOW_ONNX_FLOOR_MISMATCH
       "Link ONNX Runtime even when its minimum macOS exceeds the deployment target" OFF)

function(sattari_enable_onnx target define_name)
    if(NOT SATTARI_USE_ONNX)
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
        return()
    endif()

    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        HINTS ${ONNXRUNTIME_ROOT}/include $ENV{ONNXRUNTIME_ROOT}/include
              /opt/homebrew/include/onnxruntime /usr/local/include/onnxruntime)
    find_library(ONNXRUNTIME_LIB onnxruntime
        HINTS ${ONNXRUNTIME_ROOT}/lib $ENV{ONNXRUNTIME_ROOT}/lib
              /opt/homebrew/lib /usr/local/lib)

    if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
        # ── Deployment-floor check ───────────────────────────────────────────
        # SattariDeploymentTarget.cmake floors the binaries WE build at 10.13,
        # after the MK-ULTRA lesson: a Mach-O stamped with a newer minimum-OS
        # crashes on load on every older Mac. That floor says nothing about what
        # we LINK AGAINST. Homebrew's libonnxruntime is currently built with
        # minos 26.0, so an ONNX-enabled build silently inherits a macOS 26
        # requirement while still claiming 10.13 — the same failure, one level
        # down, and it only showed up as an easily-missed linker warning.
        #
        # Read the dylib's real floor and refuse to produce a binary whose stated
        # minimum is a lie. This is a hard error, not a warning: shipping it means
        # the plugin fails to load for almost every user.
        sattari_onnx_check_deployment_floor("${ONNXRUNTIME_LIB}" "${target}")

        # ONNX Runtime is a vendored binary SDK. Its public headers trigger a
        # large number of warnings under StemDeck's stricter warning flags; mark
        # them as system headers so project diagnostics stay visible.
        target_include_directories(${target} SYSTEM PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(${target} PRIVATE ${ONNXRUNTIME_LIB})
        target_compile_definitions(${target} PRIVATE ${define_name}=1)
        message(STATUS "Sattari ONNX: enabled for ${target} (${ONNXRUNTIME_LIB})")
    else()
        message(WARNING "SATTARI_USE_ONNX=ON but ONNX Runtime not found "
                        "(set ONNXRUNTIME_ROOT); building ${target} without it")
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
    endif()
endfunction()
