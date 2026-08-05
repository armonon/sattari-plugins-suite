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
option(SATTARI_REQUIRE_ONNX "Fail configuration when ONNX was requested but cannot be linked" OFF)

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
        target_include_directories(${target} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(${target} PRIVATE ${ONNXRUNTIME_LIB})
        target_compile_definitions(${target} PRIVATE ${define_name}=1)
        message(STATUS "Sattari ONNX: enabled for ${target} (${ONNXRUNTIME_LIB})")
    else()
        if(SATTARI_REQUIRE_ONNX)
            message(FATAL_ERROR "SATTARI_USE_ONNX=ON and SATTARI_REQUIRE_ONNX=ON, but ONNX Runtime "
                                "was not found (set ONNXRUNTIME_ROOT or the explicit include/library paths)")
        endif()
        message(WARNING "SATTARI_USE_ONNX=ON but ONNX Runtime not found "
                        "(set ONNXRUNTIME_ROOT); building ${target} without it")
        target_compile_definitions(${target} PRIVATE ${define_name}=0)
    endif()
endfunction()
