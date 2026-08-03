cmake_minimum_required(VERSION 3.22)

# ONNX is an explicit shipping choice.  In particular, merely having Homebrew's
# onnxruntime installed must not silently make a supposedly portable StemDeck
# depend on /opt/homebrew (or inherit that dylib's deployment floor).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariOnnx.cmake)

# ── Vendored ONNX Runtime ─────────────────────────────────────────────────────
# Prefer the vendored universal2 release over whatever Homebrew has. Homebrew's
# build is arm64-only AND carries a macOS 26.0 deployment floor, so it can neither
# produce a universal binary nor be shipped — which is why SATTARI_USE_ONNX was
# OFF and the entire neural stack was compiled out of the product.
#
# scripts/fetch_onnxruntime.sh downloads the official universal2 build into
# third_party/ (gitignored — a 68 MB dylib does not belong in git).
if(NOT ONNXRUNTIME_ROOT AND NOT DEFINED ENV{ONNXRUNTIME_ROOT})
    file(GLOB _sd_vendored_ort
         "${CMAKE_CURRENT_LIST_DIR}/../../third_party/onnxruntime-osx-universal2-*")
    if(_sd_vendored_ort)
        list(SORT _sd_vendored_ort)
        list(GET _sd_vendored_ort -1 ONNXRUNTIME_ROOT)   # highest version present
        message(STATUS "StemDeck: using vendored ONNX Runtime at ${ONNXRUNTIME_ROOT}")
    endif()
endif()

# StemDeck links ONNX Runtime for on-device neural separation. If the available
# runtime is NOT universal, fall back to host-arch-only for THIS plugin — the
# rest of the suite still ships universal, and an explicit
# -DCMAKE_OSX_ARCHITECTURES=... always wins.
# Must run before SattariDeploymentTarget.cmake (which sets the universal pair)
# and before project() so the compiler checks use the right arch.
if(APPLE AND SATTARI_USE_ONNX AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND uname -m
                    OUTPUT_VARIABLE _sd_host_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    foreach(_sd_ort "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
                    "$ENV{ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
                    "/opt/homebrew/lib/libonnxruntime.dylib"
                    "/usr/local/lib/libonnxruntime.dylib")
        if(EXISTS "${_sd_ort}" AND NOT _sd_ort_archs)
            execute_process(COMMAND lipo -archs "${_sd_ort}"
                            OUTPUT_VARIABLE _sd_ort_archs OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            set(_sd_ort_lib "${_sd_ort}")
        endif()
    endforeach()
    if(_sd_ort_archs AND NOT (_sd_ort_archs MATCHES "x86_64" AND _sd_ort_archs MATCHES "arm64"))
        set(CMAKE_OSX_ARCHITECTURES "${_sd_host_arch}" CACHE STRING
            "macOS build architectures" FORCE)
        message(STATUS "StemDeck: ONNX Runtime is '${_sd_ort_archs}'-only -> building "
                       "${_sd_host_arch} only (a universal build could not link).")
    endif()

    # ── Deployment floor follows the dependency ──────────────────────────────
    # The suite floors at 10.13, but no shippable onnxruntime targets anything
    # that old — arm64 macOS itself starts at 11.0. Rather than force the whole
    # suite up, or keep neural features switched off, StemDeck adopts the
    # runtime's own floor ONLY in an ONNX-enabled build. A StemDeck built without
    # ONNX keeps the 10.13 floor.
    #
    # This IS a product decision: an ONNX-enabled StemDeck will not run below the
    # reported floor. It is stated loudly at configure time rather than being
    # discovered by a user whose plugin fails to load.
    if(_sd_ort_lib AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        execute_process(COMMAND otool -l "${_sd_ort_lib}"
                        OUTPUT_VARIABLE _sd_ort_otool ERROR_QUIET)
        string(REGEX MATCH "minos[ \t]+([0-9]+(\\.[0-9]+)*)" _sd_m "${_sd_ort_otool}")
        if(_sd_m)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "${CMAKE_MATCH_1}" CACHE STRING
                "Minimum macOS version" FORCE)
            message(STATUS
                "StemDeck: SATTARI_USE_ONNX=ON -> deployment floor raised to "
                "${CMAKE_MATCH_1} to match the ONNX Runtime. A neural-enabled build "
                "will NOT load below macOS ${CMAKE_MATCH_1}. Build with "
                "-DSATTARI_USE_ONNX=OFF to keep the 10.13 floor.")
        endif()
    endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_STEMDECK_PLUGIN VERSION 0.5.0)
include(CTest)

# Shared core headers (HpcpChromagram + key profiles). TrackAnalysis.cpp uses them
# for key detection, and the EXCLUDE_FROM_ALL test targets compile that file
# standalone, so put them on the include path for every target in this directory.
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../core/include)

# Tracktion Engine requires C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariRubberBand.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariTracktionEngine.cmake)

sattari_add_plugin(
    TARGET SattariStemDeck
    SLUG stemdeck
    PRODUCT_NAME "Sattari StemDeck"
    PLUGIN_CODE Sd01
    FORMATS VST3 Standalone
    IS_SYNTH
    NEEDS_MIDI_INPUT
    EDITOR_WANTS_KEYBOARD_FOCUS
    SOURCES
        src/AnalysisCache.cpp
        src/ArrangementBuilder.cpp
        src/ArrangementView.cpp
        src/BeatSyncEngine.cpp
        src/ChunkedAudioFile.cpp
        src/DeckPlayer.cpp
        src/DeckStrip.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/HostGraph.cpp
        src/MainPluginEditor.cpp
        src/MainPluginEditor_Arrangement.cpp
        src/MainPluginEditor_Surfaces.cpp
        src/MainPluginEditor_Workflows.cpp
        src/MainPluginProcessor.cpp
        src/MasterRecorder.cpp
        src/MixerSection.cpp
        src/MusicLibraryView.cpp
        src/PerformanceRecorder.cpp
        src/PadBank.cpp
        src/RoadmapAdapters.h
        src/SamplePad.cpp
        src/SessionModel.cpp
        src/SessionCapture.cpp
        src/RealTimeStemSeparator.cpp
        src/NeuralBeatEngine.cpp
        src/NeuralStemEngine.cpp
        src/StemProvider.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp
        src/TransportSync.cpp
        src/WarpEngine.cpp
        src/WaveformDisplay.cpp)

# StemDeck additionally hosts plugins, so it needs the JUCE plugin-host flags
# on top of the shared packaging defaults.
target_compile_definitions(SattariStemDeck
    PUBLIC
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0)

sattari_enable_rubberband(SattariStemDeck SATTARI_STEMDECK_HAS_RUBBERBAND)
sattari_enable_tracktion_engine(SattariStemDeck)

# Persistent analysis cache (AnalysisCache.cpp). SQLite ships with the OS on
# macOS; on other platforms find or vendor it.
find_library(SQLITE3_LIB sqlite3)
if(SQLITE3_LIB)
    target_link_libraries(SattariStemDeck PRIVATE ${SQLITE3_LIB})
else()
    target_link_libraries(SattariStemDeck PRIVATE sqlite3)
endif()

# The EXCLUDE_FROM_ALL contract/benchmark executables below reuse SattariStemDeck's
# generated JuceHeader.h, which #includes <tracktion_graph/...>. Link the Tracktion
# module targets already created above (do NOT re-run sattari_enable_tracktion_engine
# — juce_add_module would redefine the targets) so those headers resolve + link.
function(stemdeck_link_tracktion TARGET)
    if(TARGET tracktion::tracktion_graph)
        target_link_libraries(${TARGET} PRIVATE
            tracktion::tracktion_graph
            tracktion::tracktion_core
            tracktion::tracktion_engine)
        set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
        set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD_REQUIRED ON)
        target_compile_definitions(${TARGET} PUBLIC TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0)
    endif()
endfunction()

# ── Optional: on-device neural stem separation via ONNX Runtime ───────────────
# Off by default. Enabling it routes through the suite-wide helper, which checks
# the dylib's real LC_BUILD_VERSION floor and refuses a lying shipping binary.
sattari_enable_onnx(SattariStemDeck SATTARI_STEMDECK_HAS_ONNX)
if(SATTARI_USE_ONNX AND ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    set(STEMDECK_ONNX_ENABLED ON)
    # SATTARI_HAS_ONNX is the guard used by shared neural-key headers.
    target_compile_definitions(SattariStemDeck PRIVATE SATTARI_HAS_ONNX=1)
    message(STATUS "StemDeck: on-device neural separation ENABLED (${ONNXRUNTIME_LIB})")
else()
    set(STEMDECK_ONNX_ENABLED OFF)
    target_compile_definitions(SattariStemDeck PRIVATE SATTARI_HAS_ONNX=0)
    message(STATUS "StemDeck: neural runtime disabled — using Demucs CLI + RTSS/DSP fallbacks")
endif()

# Test executables link SharedCode but compile their own translation unit with
# the same feature guards. Keep that wiring in one place so a target cannot
# accidentally re-enable a Homebrew runtime just because it was discovered.
function(stemdeck_enable_onnx TARGET)
    if(STEMDECK_ONNX_ENABLED)
        target_include_directories(${TARGET} SYSTEM PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
        target_link_libraries(${TARGET} PRIVATE ${ONNXRUNTIME_LIB})
        target_compile_definitions(${TARGET} PRIVATE
            SATTARI_STEMDECK_HAS_ONNX=1 SATTARI_HAS_ONNX=1)
    else()
        target_compile_definitions(${TARGET} PRIVATE
            SATTARI_STEMDECK_HAS_ONNX=0 SATTARI_HAS_ONNX=0)
    endif()
endfunction()

add_executable(StemDeckGranularRenderContract
    EXCLUDE_FROM_ALL
        tests/stemdeck_granular_render_contract.cpp
        src/BeatSyncEngine.cpp
        src/ChunkedAudioFile.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp)

add_dependencies(StemDeckGranularRenderContract SattariStemDeck)
stemdeck_link_tracktion(StemDeckGranularRenderContract)

target_include_directories(StemDeckGranularRenderContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckGranularRenderContract
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckGranularRenderContract
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(StemDeckTimePitchBenchmark
    EXCLUDE_FROM_ALL
        tests/stemdeck_timepitch_benchmark.cpp
        src/ChunkedAudioFile.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp)

add_dependencies(StemDeckTimePitchBenchmark SattariStemDeck)
stemdeck_link_tracktion(StemDeckTimePitchBenchmark)

target_include_directories(StemDeckTimePitchBenchmark
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckTimePitchBenchmark
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckTimePitchBenchmark
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(StemDeckTimePitchBenchmark SATTARI_STEMDECK_HAS_RUBBERBAND)

add_executable(StemDeckArchitectureContract
    EXCLUDE_FROM_ALL
        tests/stemdeck_architecture_contract.cpp
        src/ArrangementBuilder.cpp
        src/BeatSyncEngine.cpp
        src/ChunkedAudioFile.cpp
        src/DeckPlayer.cpp
        src/EQProcessor.cpp
        src/FilterProcessor.cpp
        src/HostGraph.cpp
        src/PerformanceRecorder.cpp
        src/RealTimeStemSeparator.cpp
        src/NeuralBeatEngine.cpp
        src/NeuralStemEngine.cpp
        src/StemProvider.cpp
        src/TimePitchEngine.cpp
        src/TrackAnalysis.cpp
        src/WarpEngine.cpp)

add_dependencies(StemDeckArchitectureContract SattariStemDeck)
stemdeck_link_tracktion(StemDeckArchitectureContract)

target_include_directories(StemDeckArchitectureContract
    PRIVATE
        src
        ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

target_compile_definitions(StemDeckArchitectureContract
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(StemDeckArchitectureContract
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

sattari_enable_rubberband(StemDeckArchitectureContract SATTARI_STEMDECK_HAS_RUBBERBAND)

add_executable(StemDeckBeatDetectTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_beat_detect.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckBeatDetectTest SattariStemDeck)
target_include_directories(StemDeckBeatDetectTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckBeatDetectTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckBeatDetectTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckBeatDetectTest)

# Pure decoder contract: no JUCE or ONNX dependency, so the difficult continuity
# cases remain cheap to run in every build configuration.
add_executable(StemDeckBeatGridDecoderTest
    EXCLUDE_FROM_ALL tests/stemdeck_beat_grid_decoder.cpp)
target_include_directories(StemDeckBeatGridDecoderTest PRIVATE src)
target_compile_features(StemDeckBeatGridDecoderTest PRIVATE cxx_std_17)
target_link_libraries(StemDeckBeatGridDecoderTest
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── Real full-length music accuracy gate (manifest + private/licensed audio) ──
# Links the NEURAL engines as well as the DSP analyser: the app's answer is the
# confidence-gated combination of both, so grading TrackAnalysis alone certified
# the half of the stack that was never in doubt. When ONNX is absent the gate
# still runs and reports the DSP path, and says out loud that the neural path was
# not certified.
set(STEMDECK_CORPUS_SOURCES tests/stemdeck_corpus.cpp src/TrackAnalysis.cpp)
if(STEMDECK_ONNX_ENABLED)
    list(APPEND STEMDECK_CORPUS_SOURCES src/NeuralBeatEngine.cpp)
endif()
add_executable(StemDeckCorpusGate EXCLUDE_FROM_ALL ${STEMDECK_CORPUS_SOURCES})
add_dependencies(StemDeckCorpusGate SattariStemDeck)
target_include_directories(StemDeckCorpusGate PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
target_compile_definitions(StemDeckCorpusGate PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckCorpusGate
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
# Use the shared helper rather than hand-rolling the ONNX wiring — it also sets
# SATTARI_HAS_ONNX, which the shared KeyNet header behind NeuralKeyEngine needs,
# and it is the one place that decides whether a target may use the runtime.
stemdeck_enable_onnx(StemDeckCorpusGate)
stemdeck_link_tracktion(StemDeckCorpusGate)

# ── Key-detection accuracy check (chroma + tuning comp. + profile correlation) ─
add_executable(StemDeckKeyDetectTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_key_detect.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckKeyDetectTest SattariStemDeck)
target_include_directories(StemDeckKeyDetectTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckKeyDetectTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckKeyDetectTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckKeyDetectTest)

# ── Automix planner check (Camelot + phrase mix points + tempo fold) ──────────
add_executable(StemDeckAutomixTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_automix.cpp
        src/TrackAnalysis.cpp)
add_dependencies(StemDeckAutomixTest SattariStemDeck)
target_include_directories(StemDeckAutomixTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckAutomixTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckAutomixTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckAutomixTest)

# ── Analysis cache round-trip (SQLite persistence of TrackAnalysis) ────────────
add_executable(StemDeckAnalysisCacheTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_analysis_cache.cpp
        src/AnalysisCache.cpp)
add_dependencies(StemDeckAnalysisCacheTest SattariStemDeck)
target_include_directories(StemDeckAnalysisCacheTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckAnalysisCacheTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
find_library(SQLITE3_LIB_TEST sqlite3)
target_link_libraries(StemDeckAnalysisCacheTest
    PRIVATE juce::juce_audio_utils ${SQLITE3_LIB_TEST}
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckAnalysisCacheTest)

# ── Long-file bounded cache (background I/O, immutable RT snapshots) ──────────
add_executable(StemDeckLongAudioTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_long_audio.cpp
        src/ChunkedAudioFile.cpp)
target_include_directories(StemDeckLongAudioTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckLongAudioTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckLongAudioTest
    PRIVATE juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ── Session schema round-trip (capture format: regions, tempo re-time, JSON) ───
add_executable(StemDeckSessionTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_session.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckSessionTest SattariStemDeck)
target_include_directories(StemDeckSessionTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckSessionTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckSessionTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckSessionTest)

# ── Session capture round-trip (live-performance -> event stream) ──────────────
add_executable(StemDeckCaptureTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_capture.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckCaptureTest SattariStemDeck)
target_include_directories(StemDeckCaptureTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckCaptureTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckCaptureTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckCaptureTest)

# ── Session render/replay (event stream -> deterministic sample-timed plan) ────
add_executable(StemDeckRenderTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_render.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckRenderTest SattariStemDeck)
target_include_directories(StemDeckRenderTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckRenderTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckRenderTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckRenderTest)

# ── Session renderer (event plan -> deterministic stereo mixdown) ──────────────
add_executable(StemDeckRendererTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_renderer.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckRendererTest SattariStemDeck)
target_include_directories(StemDeckRendererTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckRendererTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckRendererTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckRendererTest)

# ── Session export (render -> WAV + tracklist + cue sheet) ─────────────────────
add_executable(StemDeckExportTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_export.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckExportTest SattariStemDeck)
target_include_directories(StemDeckExportTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckExportTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckExportTest
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckExportTest)

# ── Loudness meter (BS.1770 integrated LUFS + true-peak) ───────────────────────
add_executable(StemDeckLoudnessTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_loudness.cpp)
add_dependencies(StemDeckLoudnessTest SattariStemDeck)
target_include_directories(StemDeckLoudnessTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckLoudnessTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckLoudnessTest
    PRIVATE juce::juce_audio_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ── Session demo (capture -> render -> export, writes playable files) ──────────
add_executable(StemDeckSessionDemo
    EXCLUDE_FROM_ALL
        tests/stemdeck_session_demo.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckSessionDemo SattariStemDeck)
target_include_directories(StemDeckSessionDemo PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckSessionDemo PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckSessionDemo
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckSessionDemo)

# ── Arrangement view render (draws a session to a PNG, offline) ────────────────
add_executable(StemDeckArrangeRender
    EXCLUDE_FROM_ALL
        tests/stemdeck_arrange_render.cpp
        src/ArrangementView.cpp
        src/SessionCapture.cpp
        src/SessionModel.cpp)
add_dependencies(StemDeckArrangeRender SattariStemDeck)
target_include_directories(StemDeckArrangeRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckArrangeRender PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0 JUCE_PLUGINHOST_VST=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StemDeckArrangeRender
    PRIVATE juce::juce_audio_utils juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckArrangeRender)

# ── Structure segmentation (intro/build/drop/breakdown from energy) ────────────
add_executable(StemDeckStructureTest
    EXCLUDE_FROM_ALL tests/stemdeck_structure.cpp)
add_dependencies(StemDeckStructureTest SattariStemDeck)
target_include_directories(StemDeckStructureTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckStructureTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckStructureTest
    PRIVATE juce::juce_audio_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ── StemDeck Arrange — standalone app to TRY the arrangement view on real tracks ─
juce_add_gui_app(StemDeckArrangeApp
    PRODUCT_NAME "StemDeck Arrange")
juce_generate_juce_header(StemDeckArrangeApp)
target_sources(StemDeckArrangeApp PRIVATE
    app/StemDeckArrangeApp.cpp
    src/ArrangementView.cpp
    src/SessionModel.cpp
    src/SessionCapture.cpp
    src/TrackAnalysis.cpp)
target_include_directories(StemDeckArrangeApp PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
target_compile_definitions(StemDeckArrangeApp PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="StemDeck Arrange"
    JUCE_APPLICATION_VERSION_STRING="0.1")
target_link_libraries(StemDeckArrangeApp PRIVATE
    juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    juce::juce_gui_extra juce::juce_graphics
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Toolbar icon render (verify PLAY/REC/AUTO button styling via PNG) ──────────
add_executable(StemDeckToolbarRender EXCLUDE_FROM_ALL tests/stemdeck_toolbar_render.cpp)
add_dependencies(StemDeckToolbarRender SattariStemDeck)
target_include_directories(StemDeckToolbarRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckToolbarRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckToolbarRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Neural beat engine smoke test (does the model path RUN; not accuracy) ─────
# Skips cleanly when ONNX is off or no model is installed, so it is safe in the
# ordinary contract suite. Exists because the confidence computation was #if'd
# out of every build until ONNX was enabled — it had never been compiled, and
# enabling it immediately exposed two defects in one line.
add_executable(StemDeckNeuralBeatSmoke EXCLUDE_FROM_ALL
    tests/stemdeck_neural_beat_smoke.cpp
    src/NeuralBeatEngine.cpp)
add_dependencies(StemDeckNeuralBeatSmoke SattariStemDeck)
target_include_directories(StemDeckNeuralBeatSmoke PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
target_compile_definitions(StemDeckNeuralBeatSmoke PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckNeuralBeatSmoke
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_enable_onnx(StemDeckNeuralBeatSmoke)
stemdeck_link_tracktion(StemDeckNeuralBeatSmoke)

# ── Neural key engine smoke test (does the model path RUN; not accuracy) ──────
# Same purpose as StemDeckNeuralBeatSmoke, for the key model. NeuralKeyEngine and
# its SattariKeyNet/SattariHpcpChroma dependencies are header-only, so this target
# only needs the test file itself.
add_executable(StemDeckNeuralKeySmoke EXCLUDE_FROM_ALL tests/stemdeck_neural_key_smoke.cpp)
add_dependencies(StemDeckNeuralKeySmoke SattariStemDeck)
target_include_directories(StemDeckNeuralKeySmoke PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode
    ${CMAKE_CURRENT_LIST_DIR}/../../core/include)
target_compile_definitions(StemDeckNeuralKeySmoke PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckNeuralKeySmoke
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_enable_onnx(StemDeckNeuralKeySmoke)
stemdeck_link_tracktion(StemDeckNeuralKeySmoke)

# ── Layout constraint solver (pure geometry — no editor, no audio, fast) ───────
# Guards the two regressions LayoutStack was written to kill: header items
# overlapping at narrow widths, and the stem waveform being starved by chrome.
add_executable(StemDeckLayout EXCLUDE_FROM_ALL tests/stemdeck_layout.cpp)
add_dependencies(StemDeckLayout SattariStemDeck)
target_include_directories(StemDeckLayout PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckLayout PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckLayout
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Stem gain knob render (verify the 0/100/200 arc knob via PNG) ───────────────
add_executable(StemDeckStemKnobRender EXCLUDE_FROM_ALL tests/stemdeck_stemknob_render.cpp)
add_dependencies(StemDeckStemKnobRender SattariStemDeck)
target_include_directories(StemDeckStemKnobRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckStemKnobRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckStemKnobRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Stem-scoped FX: per-stem plugin chains must re-sum phase-aligned ───────────
add_executable(StemDeckStemFx EXCLUDE_FROM_ALL
    tests/stemdeck_stemfx.cpp
    src/BeatSyncEngine.cpp
    src/ChunkedAudioFile.cpp
    src/DeckPlayer.cpp
    src/EQProcessor.cpp
    src/FilterProcessor.cpp
    src/RealTimeStemSeparator.cpp
    src/TimePitchEngine.cpp
    src/TrackAnalysis.cpp
    src/WarpEngine.cpp)
add_dependencies(StemDeckStemFx SattariStemDeck)
target_include_directories(StemDeckStemFx PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckStemFx PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckStemFx
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckStemFx)
sattari_enable_rubberband(StemDeckStemFx SATTARI_STEMDECK_HAS_RUBBERBAND)

# ── Camelot wheel render (verify wheel geometry + Camelot math via PNG) ─────────
add_executable(StemDeckCamelotRender EXCLUDE_FROM_ALL tests/stemdeck_camelot_render.cpp)
add_dependencies(StemDeckCamelotRender SattariStemDeck)
target_include_directories(StemDeckCamelotRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckCamelotRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckCamelotRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Energy-arc arranger render (ScenePilot-style §arranger) ────────────────────
add_executable(StemDeckEnergyArcRender EXCLUDE_FROM_ALL tests/stemdeck_energyarc_render.cpp)
add_dependencies(StemDeckEnergyArcRender SattariStemDeck)
target_include_directories(StemDeckEnergyArcRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckEnergyArcRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckEnergyArcRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Key-lock cores (§6.2 key curve + correction plan) ──────────────────────────
add_executable(StemDeckKeyLockTest EXCLUDE_FROM_ALL tests/stemdeck_keylock.cpp)
add_dependencies(StemDeckKeyLockTest SattariStemDeck)
target_include_directories(StemDeckKeyLockTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckKeyLockTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckKeyLockTest
    PRIVATE juce::juce_core juce::juce_audio_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Tempo panel render (verify §5 tempo map + retroactive lane via PNG) ─────────
add_executable(StemDeckTempoRender EXCLUDE_FROM_ALL tests/stemdeck_tempo_render.cpp)
add_dependencies(StemDeckTempoRender SattariStemDeck)
target_include_directories(StemDeckTempoRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckTempoRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckTempoRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Waveform region render (verify the two-zone §2 layout via PNG) ──────────────
add_executable(StemDeckWaveformRender EXCLUDE_FROM_ALL tests/stemdeck_waveform_render.cpp)
add_dependencies(StemDeckWaveformRender SattariStemDeck)
target_include_directories(StemDeckWaveformRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckWaveformRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckWaveformRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Whole-editor render (the only PNG gate that shows the UI IN CONTEXT) ───────
# Links the plugin's SharedCode so it instantiates the real processor + editor.
add_executable(StemDeckEditorRender EXCLUDE_FROM_ALL tests/stemdeck_editor_render.cpp)
target_include_directories(StemDeckEditorRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckEditorRender PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)   # runDispatchLoopUntil, to settle async layout
target_link_libraries(StemDeckEditorRender
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckEditorRender)
sattari_enable_rubberband(StemDeckEditorRender SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckEditorRender)

# ── A1 phase 5: THE GATE — replay must be sample-identical to the live pass ────
add_executable(StemDeckReplayHash EXCLUDE_FROM_ALL tests/stemdeck_replayhash.cpp)
target_include_directories(StemDeckReplayHash PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckReplayHash PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckReplayHash
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckReplayHash)
sattari_enable_rubberband(StemDeckReplayHash SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckReplayHash)

# Recording button -> deterministic intent stream + health telemetry contract.
add_executable(StemDeckRecordingHealth EXCLUDE_FROM_ALL tests/stemdeck_recording_health.cpp)
target_include_directories(StemDeckRecordingHealth PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckRecordingHealth PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckRecordingHealth
    PRIVATE SattariStemDeck
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckRecordingHealth)
sattari_enable_rubberband(StemDeckRecordingHealth SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckRecordingHealth)

# Permanent channel identity and single-path live-input monitoring regression gate.
add_executable(StemDeckChannelIdentity EXCLUDE_FROM_ALL tests/stemdeck_channel_identity.cpp)
target_include_directories(StemDeckChannelIdentity PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckChannelIdentity PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckChannelIdentity
    PRIVATE SattariStemDeck
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckChannelIdentity)
sattari_enable_rubberband(StemDeckChannelIdentity SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckChannelIdentity)

# ── A1 phase 3: dispatch and replay are the same code path ────────────────────
add_executable(StemDeckDispatch EXCLUDE_FROM_ALL tests/stemdeck_dispatch.cpp)
add_dependencies(StemDeckDispatch SattariStemDeck)
target_include_directories(StemDeckDispatch PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckDispatch PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckDispatch
    PRIVATE juce::juce_core
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── A4: live input/output through a REAL audio device (loopback) ──────────────
add_executable(StemDeckLiveIO EXCLUDE_FROM_ALL tests/stemdeck_liveio.cpp)
target_include_directories(StemDeckLiveIO PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckLiveIO PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckLiveIO
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckLiveIO)
sattari_enable_rubberband(StemDeckLiveIO SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckLiveIO)

# ── A1 phase 1: event stream round-trips exactly and refuses what it must ──────
add_executable(StemDeckEventStream EXCLUDE_FROM_ALL tests/stemdeck_eventstream.cpp)
add_dependencies(StemDeckEventStream SattariStemDeck)
target_include_directories(StemDeckEventStream PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckEventStream PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckEventStream
    PRIVATE juce::juce_core
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── A2: the audio thread must not allocate (enforced by an operator new hook) ──
add_executable(StemDeckRtSafety EXCLUDE_FROM_ALL tests/stemdeck_rtsafety.cpp)
target_include_directories(StemDeckRtSafety PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckRtSafety PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckRtSafety
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckRtSafety)
sattari_enable_rubberband(StemDeckRtSafety SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckRtSafety)

# ── Multi-instance: a DAW can host two StemDecks in one process ────────────────
add_executable(StemDeckMultiInstance EXCLUDE_FROM_ALL tests/stemdeck_multiinstance.cpp)
target_include_directories(StemDeckMultiInstance PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckMultiInstance PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckMultiInstance
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckMultiInstance)
sattari_enable_rubberband(StemDeckMultiInstance SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckMultiInstance)

# ── A3: measure time-to-playable at real track lengths ─────────────────────────
add_executable(StemDeckLoadLatency EXCLUDE_FROM_ALL tests/stemdeck_loadlatency.cpp)
target_include_directories(StemDeckLoadLatency PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckLoadLatency PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(StemDeckLoadLatency
    PRIVATE SattariStemDeck
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckLoadLatency)
sattari_enable_rubberband(StemDeckLoadLatency SATTARI_STEMDECK_HAS_RUBBERBAND)
stemdeck_enable_onnx(StemDeckLoadLatency)

# ── Deck strip render (verify the per-deck control strip styling via PNG) ───────
add_executable(StemDeckDeckStripRender EXCLUDE_FROM_ALL
    tests/stemdeck_deckstrip_render.cpp
    src/DeckStrip.cpp)
add_dependencies(StemDeckDeckStripRender SattariStemDeck)
target_include_directories(StemDeckDeckStripRender PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckDeckStripRender PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckDeckStripRender
    PRIVATE juce::juce_gui_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ── Resampler quality check (linear vs windowed-sinc interpolation) ────────────
# Header-only (Interpolation.h) + juce_audio_basics; no engine deps.
# ── Bundle the ONNX models into the .app / .vst3 Resources ────────────────────
# ModelLocator.h searches: env override -> bundle Resources -> ~/Library install.
# Copying the models in makes a packaged StemDeck self-contained (no manual install),
# but they are large (neural ~353MB + beat ~84MB), so this is OPT-IN and off by
# default to keep dev builds fast:
#     cmake -S plugins/stemdeck -B build/stemdeck -DSTEMDECK_BUNDLE_MODELS=ON
option(STEMDECK_BUNDLE_MODELS "Copy the ONNX models into the app/plugin bundle Resources" OFF)

if(STEMDECK_BUNDLE_MODELS)
    set(_sd_model_dir "${CMAKE_CURRENT_LIST_DIR}/../../models")
    set(_sd_models "")
    foreach(_m "neural-model.onnx" "beat-model.onnx")
        if(EXISTS "${_sd_model_dir}/${_m}")
            list(APPEND _sd_models "${_sd_model_dir}/${_m}")
        else()
            message(WARNING "StemDeck: STEMDECK_BUNDLE_MODELS=ON but ${_m} is missing from ${_sd_model_dir} — the app will fall back to the ~/Library install or the DSP path.")
        endif()
    endforeach()

    if(_sd_models)
        # Standalone .app — Contents/Resources (what ModelLocator checks first).
        add_custom_command(TARGET SattariStemDeck_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_BUNDLE_CONTENT_DIR:SattariStemDeck_Standalone>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sd_models}
                    "$<TARGET_BUNDLE_CONTENT_DIR:SattariStemDeck_Standalone>/Resources"
            COMMENT "StemDeck: bundling ONNX models into the standalone app")

        if(TARGET SattariStemDeck_VST3)
            add_custom_command(TARGET SattariStemDeck_VST3 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_BUNDLE_CONTENT_DIR:SattariStemDeck_VST3>/Resources"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sd_models}
                        "$<TARGET_BUNDLE_CONTENT_DIR:SattariStemDeck_VST3>/Resources"
                COMMENT "StemDeck: bundling ONNX models into the VST3")
        endif()
        message(STATUS "StemDeck: model bundling ENABLED (${_sd_models})")
    endif()
endif()

# ── Mute / solo arbitration contract ───────────────────────────────────────────
add_executable(StemDeckSolo EXCLUDE_FROM_ALL tests/stemdeck_solo.cpp)
add_dependencies(StemDeckSolo SattariStemDeck)
target_include_directories(StemDeckSolo PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckSolo PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckSolo
    PRIVATE juce::juce_audio_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckSolo)

# ── MIDI capture contract (what you played -> piano roll) ──────────────────────
add_executable(StemDeckMidiCapture EXCLUDE_FROM_ALL tests/stemdeck_midicapture.cpp)
add_dependencies(StemDeckMidiCapture SattariStemDeck)
target_include_directories(StemDeckMidiCapture PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckMidiCapture PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckMidiCapture
    PRIVATE juce::juce_audio_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckMidiCapture)

# ── Plugin delay compensation contract (§3.3) ──────────────────────────────────
add_executable(StemDeckPdc EXCLUDE_FROM_ALL tests/stemdeck_pdc.cpp)
add_dependencies(StemDeckPdc SattariStemDeck)
target_include_directories(StemDeckPdc PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckPdc PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckPdc
    PRIVATE juce::juce_audio_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckPdc)

# ── Zero-dropout add/remove-under-playback contract (channel release gate) ─────
add_executable(StemDeckChannelDropout EXCLUDE_FROM_ALL tests/stemdeck_channel_dropout.cpp)
add_dependencies(StemDeckChannelDropout SattariStemDeck)
target_include_directories(StemDeckChannelDropout PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckChannelDropout PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckChannelDropout
    PRIVATE juce::juce_audio_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckChannelDropout)

# ── TrackDeckBody render test (Step 3): per-deck render via ChannelBody ─────────
add_executable(StemDeckTrackDeckBody EXCLUDE_FROM_ALL
    tests/stemdeck_trackdeckbody.cpp
    src/BeatSyncEngine.cpp
    src/ChunkedAudioFile.cpp
    src/DeckPlayer.cpp
    src/EQProcessor.cpp
    src/FilterProcessor.cpp
    src/RealTimeStemSeparator.cpp
    src/TimePitchEngine.cpp
    src/TrackAnalysis.cpp
    src/WarpEngine.cpp)
add_dependencies(StemDeckTrackDeckBody SattariStemDeck)
target_include_directories(StemDeckTrackDeckBody PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckTrackDeckBody PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckTrackDeckBody
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckTrackDeckBody)
sattari_enable_rubberband(StemDeckTrackDeckBody SATTARI_STEMDECK_HAS_RUBBERBAND)

# ── Channel-architecture scaffolding compile + smoke check (Step 1a) ───────────
add_executable(StemDeckChannelCompile EXCLUDE_FROM_ALL tests/stemdeck_channel_compile.cpp)
add_dependencies(StemDeckChannelCompile SattariStemDeck)
target_include_directories(StemDeckChannelCompile PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckChannelCompile PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckChannelCompile
    PRIVATE juce::juce_audio_basics juce::juce_graphics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
stemdeck_link_tracktion(StemDeckChannelCompile)   # JuceHeader.h pulls tracktion_graph

add_executable(StemDeckResamplerTest
    EXCLUDE_FROM_ALL
        tests/stemdeck_resampler.cpp)
add_dependencies(StemDeckResamplerTest SattariStemDeck)
target_include_directories(StemDeckResamplerTest PRIVATE
    src ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)
target_compile_definitions(StemDeckResamplerTest PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(StemDeckResamplerTest
    PRIVATE juce::juce_audio_basics
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ── Neural separation smoke test (real audio → 4 stems via ONNX Runtime) ───────
# Exercises the app's exact NeuralStemEngine + ORT path headlessly. Only built
# when ONNX Runtime is present.
if(STEMDECK_ONNX_ENABLED)
    add_executable(StemDeckNeuralSmoke
        EXCLUDE_FROM_ALL
            tests/stemdeck_neural_smoke.cpp
            src/NeuralStemEngine.cpp)

    add_dependencies(StemDeckNeuralSmoke SattariStemDeck)

    target_include_directories(StemDeckNeuralSmoke
        PRIVATE
            src
            ${ONNXRUNTIME_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

    target_compile_definitions(StemDeckNeuralSmoke
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_PLUGINHOST_AU=1
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_LV2=0
            JUCE_PLUGINHOST_VST=0
            JUCE_VST3_CAN_REPLACE_VST2=0
        PRIVATE
            SATTARI_STEMDECK_HAS_ONNX=1)

    target_link_libraries(StemDeckNeuralSmoke
        PRIVATE
            juce::juce_audio_utils
            ${ONNXRUNTIME_LIB}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    stemdeck_link_tracktion(StemDeckNeuralSmoke)

    # ── Deterministic stem-separation cancellation test ────────────────────────
    # NeuralStemEngine::separate() already implements cancellation (shouldCancel +
    # cancelPending()/SetTerminate, mirroring NeuralBeatEngine's pattern) — an
    # audit initially assumed this needed to be BUILT, and found on inspection
    # that it needed to be RUN: it had never actually been exercised. This runs
    # the real model and cancels it mid-flight, on a background thread, with a
    # bounded-latency assertion. Zero-arg (unlike StemDeckNeuralSmoke, which needs
    # a WAV path), so it goes in the automatic contract suite below and skips
    # cleanly when no separation model is installed.
    add_executable(StemDeckStemCancelSmoke
        EXCLUDE_FROM_ALL
            tests/stemdeck_stemcancel_smoke.cpp
            src/NeuralStemEngine.cpp)

    add_dependencies(StemDeckStemCancelSmoke SattariStemDeck)

    target_include_directories(StemDeckStemCancelSmoke
        PRIVATE
            src
            ${ONNXRUNTIME_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/SattariStemDeck_artefacts/JuceLibraryCode)

    target_compile_definitions(StemDeckStemCancelSmoke
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_PLUGINHOST_AU=1
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_LV2=0
            JUCE_PLUGINHOST_VST=0
            JUCE_VST3_CAN_REPLACE_VST2=0
        PRIVATE
            SATTARI_STEMDECK_HAS_ONNX=1)

    target_link_libraries(StemDeckStemCancelSmoke
        PRIVATE
            juce::juce_audio_utils
            ${ONNXRUNTIME_LIB}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    stemdeck_link_tracktion(StemDeckStemCancelSmoke)
endif()

# ── CTest contract suite ──────────────────────────────────────────────────────
# Keep the render fixtures and corpus/live-I/O tools opt-in: they need output
# paths, real media, or hardware. The tests below are deterministic, zero-arg
# contracts and can be run uniformly by local builds and CI.
if(BUILD_TESTING)
    set(STEMDECK_CONTRACT_TESTS
        StemDeckArchitectureContract
        StemDeckBeatDetectTest
        StemDeckBeatGridDecoderTest
        StemDeckKeyDetectTest
        StemDeckAutomixTest
        StemDeckAnalysisCacheTest
        StemDeckLongAudioTest
        StemDeckSessionTest
        StemDeckCaptureTest
        StemDeckRenderTest
        StemDeckRendererTest
        StemDeckExportTest
        StemDeckLoudnessTest
        StemDeckStructureTest
        StemDeckLayout
        StemDeckStemFx
        StemDeckKeyLockTest
        StemDeckReplayHash
        StemDeckRecordingHealth
        StemDeckChannelIdentity
        StemDeckDispatch
        StemDeckEventStream
        StemDeckRtSafety
        StemDeckMultiInstance
        StemDeckLoadLatency
        StemDeckSolo
        StemDeckMidiCapture
        StemDeckPdc
        StemDeckChannelDropout
        StemDeckTrackDeckBody
        StemDeckChannelCompile
        StemDeckResamplerTest
        StemDeckNeuralBeatSmoke
        StemDeckNeuralKeySmoke
        StemDeckStemCancelSmoke)

    foreach(_sd_test IN LISTS STEMDECK_CONTRACT_TESTS)
        if(TARGET ${_sd_test})
            add_test(NAME ${_sd_test} COMMAND $<TARGET_FILE:${_sd_test}>)
            # StemDeckStemCancelSmoke runs the real ~370MB separation model,
            # CPU-bound, four times (including a 30s waits-for-progress step) —
            # 120s is tight for that specific test even though it's generous for
            # every other zero-arg contract, so it alone gets more room.
            if(_sd_test STREQUAL "StemDeckStemCancelSmoke")
                set_tests_properties(${_sd_test} PROPERTIES LABELS "stemdeck;contract" TIMEOUT 300)
            else()
                set_tests_properties(${_sd_test} PROPERTIES LABELS "stemdeck;contract" TIMEOUT 120)
            endif()
            # This contract intentionally measures cold loading of a five-minute
            # asset. Running it beside the neural model stress tests measures CPU
            # contention rather than the load path and creates a false failure.
            if(_sd_test STREQUAL "StemDeckLoadLatency")
                set_tests_properties(${_sd_test} PROPERTIES RUN_SERIAL TRUE)
            endif()
        endif()
    endforeach()

    # The corpus gate itself needs private audio and stays opt-in, but its
    # generated-fixture DETECTOR carries a claim ("real music only") and must stay
    # falsifiable, so its self-test runs with the ordinary contracts. It caught a
    # real defect the first time it ran — the detector was passing this repo's own
    # sine fixture — which is exactly why it is wired here and not run by hand.
    if(TARGET StemDeckCorpusGate)
        add_test(NAME StemDeckCorpusSelfTest
                 COMMAND $<TARGET_FILE:StemDeckCorpusGate> --selftest)
        set_tests_properties(StemDeckCorpusSelfTest PROPERTIES
            LABELS "stemdeck;contract"
            TIMEOUT 120)
    endif()

    # StemDeckCorpusGate is EXCLUDE_FROM_ALL and not a contract itself, but its
    # --selftest IS registered above, so the suite has to build the binary or that
    # test cannot run.
    set(STEMDECK_CONTRACT_SUITE_DEPS ${STEMDECK_CONTRACT_TESTS})
    if(TARGET StemDeckCorpusGate)
        list(APPEND STEMDECK_CONTRACT_SUITE_DEPS StemDeckCorpusGate)
    endif()
    add_custom_target(StemDeckContractSuite
        DEPENDS ${STEMDECK_CONTRACT_SUITE_DEPS}
        COMMENT "Building the deterministic StemDeck contract suite")
endif()
