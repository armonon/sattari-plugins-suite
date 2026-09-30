cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/SattariDeploymentTarget.cmake)
project(SATTARI_AUDIO_PLUGINS VERSION 0.1.0)

add_subdirectory(core)

add_subdirectory(plugins/mix)
add_subdirectory(plugins/create)
add_subdirectory(plugins/vocal)
add_subdirectory(plugins/stack)
add_subdirectory(plugins/sub-conjurer)
option(SATTARI_INCLUDE_STEMDECK "Configure the separately-gated StemDeck product" OFF)
if(SATTARI_INCLUDE_STEMDECK)
    add_subdirectory(plugins/stemdeck)
endif()
add_subdirectory(plugins/side-chain-master)
add_subdirectory(plugins/royal-chain)
add_subdirectory(plugins/harmony)
add_subdirectory(plugins/maqam)
add_subdirectory(plugins/vocal-master)
add_subdirectory(plugins/rhythm-genie)
add_subdirectory(plugins/chop)
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

# Renders the whole shared control vocabulary, every state, every product accent,
# to one PNG. SattariControlSurface.h is inherited by all 49 editors, so this is
# how a change to it gets reviewed without opening plugins in a DAW one at a time.
if(TARGET juce::juce_audio_utils)
    add_executable(SattariControlGallery
        EXCLUDE_FROM_ALL
        core/tests/control_gallery.cpp)
    target_link_libraries(SattariControlGallery
        PRIVATE
            SattariAudioCore
            juce::juce_audio_utils
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

if(TARGET juce::juce_audio_utils)
    add_executable(SattariRackHostContract
        EXCLUDE_FROM_ALL
        core/tests/rack_host_contract.cpp)
    target_link_libraries(SattariRackHostContract
        PRIVATE
            SattariAudioCore
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# Shared KeyAnalysisEngine contract. It intentionally compiles with ONNX off:
# neural inference may enhance Compass, but missing model assets must always
# leave the complete chroma path available and correctly attributed.
if(TARGET juce::juce_dsp)
    add_executable(SattariKeyAnalysisContract
        EXCLUDE_FROM_ALL
        core/tests/key_analysis_contract.cpp)
    target_compile_definitions(SattariKeyAnalysisContract
        PRIVATE SATTARI_HAS_ONNX=0)
    target_link_libraries(SattariKeyAnalysisContract
        PRIVATE
            SattariAudioCore
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(SattariHarmonyVoicesContract
        EXCLUDE_FROM_ALL
        core/tests/harmony_voices_contract.cpp)
    target_compile_definitions(SattariHarmonyVoicesContract
        PRIVATE SATTARI_HAS_ONNX=0)
    target_link_libraries(SattariHarmonyVoicesContract
        PRIVATE
            SattariAudioCore
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(SattariExtremeLivePitchContract
        EXCLUDE_FROM_ALL
        core/tests/extreme_live_pitch_contract.cpp)
    target_link_libraries(SattariExtremeLivePitchContract
        PRIVATE
            SattariAudioCore
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(SattariChordAnalysisContract
        EXCLUDE_FROM_ALL
        core/tests/chord_analysis_contract.cpp)
    target_compile_definitions(SattariChordAnalysisContract
        PRIVATE SATTARI_HAS_ONNX=0)
    target_link_libraries(SattariChordAnalysisContract
        PRIVATE
            SattariAudioCore
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(SattariSongKeyEstimatorContract
        EXCLUDE_FROM_ALL
        core/tests/song_key_estimator_contract.cpp)
    target_compile_definitions(SattariSongKeyEstimatorContract
        PRIVATE SATTARI_HAS_ONNX=0)
    target_link_libraries(SattariSongKeyEstimatorContract
        PRIVATE
            SattariAudioCore
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(SattariDynamicKeyTrackerContract
        EXCLUDE_FROM_ALL
        core/tests/dynamic_key_tracker_contract.cpp)
    target_compile_definitions(SattariDynamicKeyTrackerContract
        PRIVATE SATTARI_HAS_ONNX=0)
    target_link_libraries(SattariDynamicKeyTrackerContract
        PRIVATE
            SattariAudioCore
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# Root-suite CTest inventory. The executables remain explicit build targets so a
# developer can build a focused contract, while `ctest` finally provides standard
# discovery/reporting for a prepared root tree.
include(CTest)
if(BUILD_TESTING)
    set(SATTARI_CONTRACT_TARGETS
        SattariChordAnalysisContract
        SattariDynamicKeyTrackerContract
        SattariHarmonyVoicesContract
        SattariExtremeLivePitchContract
        SattariAudioCorePitchContract
        SattariAudioCoreHarmonicContract
        SattariAudioCoreKeyContract
        SattariAudioCoreTempoContract
        SattariAudioCoreScaleIntervalContract
        SattariAudioCoreKWeightingContract
        SattariAudioCoreDrumKitContract
        SattariAudioCoreDrumPadsContract
        SattariAudioCoreDrumLooperContract
        SattariAudioCoreBeatGridContract
        SattariAudioCoreGrooveContract
        SattariAudioCoreStepClockContract
        SattariAudioCoreArrangementContract
        SattariAudioCoreNoteEngineContract
        SattariAudioCorePsolaContract
        SattariAudioCoreLivePitchContract
        SattariAudioCoreCrepeContract
        SattariAudioCorePitchFusionContract
        SattariAudioCoreVoxContract
        SattariAudioCoreVoxSynthContract
        SattariAudioCoreFormantContract
        SattariAudioCoreVocoderContract
        SattariAudioCoreSliceTableContract
        SattariAudioCoreOnsetCurveContract
        SattariAudioCoreHarmonicMaskContract
        SattariAudioCoreRealtimeSnapshotContract
        SattariKeyAnalysisContract
        SattariSongKeyEstimatorContract
        SattariCrossPluginBusContract
        SattariRackHostContract
        SattariVocalProductContract
        SattariMixProductContract
        SattariCreateProductContract
        AutoPitchCoreContract
        VoxKeyCoreContract
        SattariArpTimingContract
        SattariBusCoreContract
        SideChainMasterCoreContract
        SattariAlignCoreContract
        RoyalChainColorAliasingContract
        HarmonyCoreContract
        MaqamCoreContract
        RhythmGenieCoreContract
        RhythmGenieTimingContract
        VocalMasterCoreContract
        SubConjurerContract
        WarpContract
        TunerContract
        DoubleContract
        DeEssContract
        FaceContract
        TransientContract
        MeterContract
        EQContract
        ScalerContract
        SpaceContract
        CleanContract
        CompContract
        PeakContract
        VocoderContract
        ShimmerContract
        EchoContract
        HeatContract
        MultibandContract
        GateContract
        RiserContract
        PulseContract
        Sattari808Contract
        ImagerContract
        WarbleContract
        ScopeContract
        GranulatorContract
        GlassChoirContract
        BrainContract
        VoxSynthMpeContract
        SattariStackLayerContract)

    foreach(contract IN LISTS SATTARI_CONTRACT_TARGETS)
        if(TARGET ${contract})
            add_test(NAME ${contract} COMMAND $<TARGET_FILE:${contract}>)
            set_tests_properties(${contract} PROPERTIES LABELS "contract;non-stemdeck")
            if(contract STREQUAL "SattariAudioCoreLivePitchContract")
                # Its callback percentile gate measures one engine on one core;
                # parallel contracts would turn scheduler contention into a false
                # DSP regression.
                set_tests_properties(${contract} PROPERTIES RUN_SERIAL TRUE)
            endif()
        endif()
    endforeach()

    foreach(ui_product IN ITEMS
            "BrainContract;brain"
            "SattariArpTimingContract;sattari-arp"
            "SubConjurerContract;sub-conjurer")
        list(GET ui_product 0 ui_target)
        list(GET ui_product 1 ui_slug)
        if(TARGET ${ui_target})
            add_test(
                NAME ${ui_target}EditorRender
                COMMAND $<TARGET_FILE:${ui_target}>
                    --render-ui
                    ${CMAKE_BINARY_DIR}/validation/ui/${ui_slug}.png)
            set_tests_properties(
                ${ui_target}EditorRender
                PROPERTIES LABELS "ui;render;non-stemdeck")
        endif()
    endforeach()

    if(TARGET SattariArpTimingContract)
        add_test(
            NAME SattariArpTimingContractEditorMinimumRender
            COMMAND $<TARGET_FILE:SattariArpTimingContract>
                --render-ui-min
                ${CMAKE_BINARY_DIR}/validation/ui/sattari-arp-minimum.png)
        set_tests_properties(
            SattariArpTimingContractEditorMinimumRender
            PROPERTIES LABELS "ui;render;minimum;non-stemdeck")
    endif()

    if(TARGET SattariRackHostContract)
        foreach(rack_ui_product IN ITEMS
                "SattariMix;mix"
                "SattariCreate;create"
                "SattariVocal;vocal"
                "SattariStack;stack")
            list(GET rack_ui_product 0 rack_ui_name)
            list(GET rack_ui_product 1 rack_ui_slug)
            add_test(
                NAME ${rack_ui_name}EditorRender
                COMMAND $<TARGET_FILE:SattariRackHostContract>
                    --render-ui
                    ${rack_ui_slug}
                    ${CMAKE_BINARY_DIR}/validation/ui/${rack_ui_slug}.png)
            set_tests_properties(
                ${rack_ui_name}EditorRender
                PROPERTIES LABELS "ui;render;non-stemdeck")
            add_test(
                NAME ${rack_ui_name}EditorMinimumRender
                COMMAND $<TARGET_FILE:SattariRackHostContract>
                    --render-ui-min
                    ${rack_ui_slug}
                    ${CMAKE_BINARY_DIR}/validation/ui/${rack_ui_slug}-minimum.png)
            set_tests_properties(
                ${rack_ui_name}EditorMinimumRender
                PROPERTIES LABELS "ui;render;minimum;non-stemdeck")
        endforeach()
    endif()

    foreach(analysis_ui_product IN ITEMS
            "MeterContract;meter"
            "ScopeContract;scope"
            "TunerContract;tuner"
            "SongKeyRender;song-key")
        list(GET analysis_ui_product 0 analysis_ui_target)
        list(GET analysis_ui_product 1 analysis_ui_slug)
        if(TARGET ${analysis_ui_target})
            add_test(
                NAME ${analysis_ui_target}EditorRender
                COMMAND $<TARGET_FILE:${analysis_ui_target}>
                    --render-ui
                    ${CMAKE_BINARY_DIR}/validation/ui/${analysis_ui_slug}.png)
            add_test(
                NAME ${analysis_ui_target}EditorMinimumRender
                COMMAND $<TARGET_FILE:${analysis_ui_target}>
                    --render-ui-min
                    ${CMAKE_BINARY_DIR}/validation/ui/${analysis_ui_slug}-minimum.png)
            set_tests_properties(
                ${analysis_ui_target}EditorRender
                PROPERTIES LABELS "ui;render;analysis;non-stemdeck")
            set_tests_properties(
                ${analysis_ui_target}EditorMinimumRender
                PROPERTIES LABELS "ui;render;analysis;minimum;non-stemdeck")
        endif()
    endforeach()

    if(TARGET SongKeyRender)
        add_test(NAME SongKeyRenderSmoke COMMAND $<TARGET_FILE:SongKeyRender> --smoke)
        add_test(NAME SongKeyFileAnalysisSmoke COMMAND $<TARGET_FILE:SongKeyRender> --file-smoke)
        add_test(NAME SongKeyLiveTempoSmoke COMMAND $<TARGET_FILE:SongKeyRender> --live-tempo-smoke)
        add_test(NAME SongKeyDynamicFollowSmoke COMMAND $<TARGET_FILE:SongKeyRender> --dynamic-follow-smoke)
        add_test(NAME SongKeyCadenceSmoke COMMAND $<TARGET_FILE:SongKeyRender> --cadence-smoke)
        add_test(NAME SongKeyNonFiniteSmoke COMMAND $<TARGET_FILE:SongKeyRender> --nonfinite-smoke)
        set_tests_properties(
            SongKeyRenderSmoke SongKeyFileAnalysisSmoke SongKeyLiveTempoSmoke
            SongKeyDynamicFollowSmoke SongKeyCadenceSmoke SongKeyNonFiniteSmoke
            PROPERTIES LABELS "render;non-stemdeck")
    endif()
    if(TARGET AutoPitchRender)
        add_test(NAME AutoPitchRenderSmoke COMMAND $<TARGET_FILE:AutoPitchRender> --smoke)
        add_test(NAME AutoPitchHighTracking COMMAND $<TARGET_FILE:AutoPitchRender> --hitracktest)
        set_tests_properties(
            AutoPitchRenderSmoke AutoPitchHighTracking
            PROPERTIES LABELS "render;non-stemdeck")
    endif()
endif()
