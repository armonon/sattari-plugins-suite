# =============================================================================
#  Sattari Audio - shared plugin packaging path
# -----------------------------------------------------------------------------
#  One call wraps juce_add_plugin with the Sattari defaults proven by the
#  pluginval / auval-validated builds: AU + VST3 + Standalone, shared-core link,
#  recommended JUCE flags, and copy-after-build. This lifts the ~40 lines of
#  per-plugin boilerplate out of every product so the packaging pipeline is
#  built and maintained once - the foundation every Sattari product ships on.
#
#  A plugin's CMakeLists collapses to:
#
#     cmake_minimum_required(VERSION 3.22)
#     project(MY_PLUGIN VERSION 0.1.0)
#     include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
#     sattari_add_plugin(
#         TARGET MyPlugin
#         SLUG   my-plugin
#         PRODUCT_NAME "My Plugin"
#         PLUGIN_CODE  Mp01
#         NEEDS_MIDI_INPUT          # option flags as needed
#         SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp)
#
#  AU is on by default so Logic/GarageBand support is the norm, not an
#  afterthought (it was the gap that left StemDeck VST3-only).
# =============================================================================

include_guard(GLOBAL)

# Repo root resolved from this file's location (<root>/cmake/SattariPlugin.cmake).
get_filename_component(SATTARI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(sattari_add_plugin)
    set(options       IS_SYNTH NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT IS_MIDI_EFFECT EDITOR_WANTS_KEYBOARD_FOCUS)
    set(oneValueArgs  TARGET SLUG PRODUCT_NAME PLUGIN_CODE MANUFACTURER_CODE COMPANY_NAME VERSION)
    set(multiValueArgs FORMATS SOURCES)
    cmake_parse_arguments(SAP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SAP_TARGET)
        message(FATAL_ERROR "sattari_add_plugin: TARGET is required")
    endif()
    if(NOT SAP_PLUGIN_CODE)
        message(FATAL_ERROR "sattari_add_plugin(${SAP_TARGET}): PLUGIN_CODE is required")
    endif()
    if(NOT SAP_SOURCES)
        message(FATAL_ERROR "sattari_add_plugin(${SAP_TARGET}): SOURCES is required")
    endif()

    # Sattari-wide defaults.
    if(NOT SAP_COMPANY_NAME)
        set(SAP_COMPANY_NAME "Sattari")
    endif()
    if(NOT SAP_MANUFACTURER_CODE)
        set(SAP_MANUFACTURER_CODE "Satr")
    endif()
    if(NOT SAP_FORMATS)
        set(SAP_FORMATS AU VST3 Standalone)
    endif()
    if(NOT SAP_PRODUCT_NAME)
        set(SAP_PRODUCT_NAME "${SAP_TARGET}")
    endif()
    if(NOT SAP_VERSION)
        set(SAP_VERSION "${PROJECT_VERSION}")
    endif()

    # Make JUCE + the shared core available whether building from the repo root
    # (already added) or from a single plugin directory (added here on demand).
    if(NOT TARGET juce::juce_audio_utils)
        add_subdirectory("${SATTARI_REPO_ROOT}/third_party/JUCE" "${CMAKE_BINARY_DIR}/third_party/JUCE")
    endif()
    if(NOT TARGET SattariAudioCore)
        add_subdirectory("${SATTARI_REPO_ROOT}/core" "${CMAKE_BINARY_DIR}/core")
    endif()
    if(NOT TARGET SattariBrandAssets)
        juce_add_binary_data(SattariBrandAssets
            HEADER_NAME SattariBrandAssets.h
            NAMESPACE SattariBrandAssetsData
            SOURCES
                "${SATTARI_REPO_ROOT}/assets/branding/sattari-logo-white.png")
        target_sources(SattariBrandAssets
            PRIVATE
                "${SATTARI_REPO_ROOT}/core/src/SattariLogo.cpp")
        target_include_directories(SattariBrandAssets
            PRIVATE
                "${SATTARI_REPO_ROOT}/core/include"
                "${SATTARI_REPO_ROOT}/third_party/JUCE/modules"
                "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_SattariBrandAssets/JuceLibraryCode")
        target_compile_definitions(SattariBrandAssets
            PRIVATE
                JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
                JUCE_MODULE_AVAILABLE_juce_core=1
                JUCE_MODULE_AVAILABLE_juce_events=1
                JUCE_MODULE_AVAILABLE_juce_graphics=1
                $<$<CONFIG:Debug>:DEBUG=1>
                $<$<CONFIG:Debug>:_DEBUG=1>
                $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)
        target_compile_features(SattariBrandAssets PUBLIC cxx_std_17)
        target_link_libraries(SattariAudioCore INTERFACE SattariBrandAssets)
    endif()

    # Option flags -> TRUE/FALSE for juce_add_plugin.
    foreach(flag IN ITEMS IS_SYNTH NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT IS_MIDI_EFFECT EDITOR_WANTS_KEYBOARD_FOCUS)
        if(SAP_${flag})
            set(${flag}_VAL TRUE)
        else()
            set(${flag}_VAL FALSE)
        endif()
    endforeach()

    juce_add_plugin(${SAP_TARGET}
        COMPANY_NAME                "${SAP_COMPANY_NAME}"
        IS_SYNTH                    ${IS_SYNTH_VAL}
        NEEDS_MIDI_INPUT            ${NEEDS_MIDI_INPUT_VAL}
        NEEDS_MIDI_OUTPUT           ${NEEDS_MIDI_OUTPUT_VAL}
        IS_MIDI_EFFECT              ${IS_MIDI_EFFECT_VAL}
        EDITOR_WANTS_KEYBOARD_FOCUS ${EDITOR_WANTS_KEYBOARD_FOCUS_VAL}
        COPY_PLUGIN_AFTER_BUILD     TRUE
        PLUGIN_MANUFACTURER_CODE    ${SAP_MANUFACTURER_CODE}
        PLUGIN_CODE                 ${SAP_PLUGIN_CODE}
        FORMATS                     ${SAP_FORMATS}
        VERSION                     ${SAP_VERSION}
        PRODUCT_NAME                "${SAP_PRODUCT_NAME}")

    juce_generate_juce_header(${SAP_TARGET})

    target_sources(${SAP_TARGET} PRIVATE ${SAP_SOURCES})

    target_compile_definitions(${SAP_TARGET}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            # The suite ships under GPLv3 (see LICENSING.md), which exempts it from
            # JUCE's free-tier splash-screen requirement. Do NOT set this under a
            # JUCE "Personal" (non-GPL) licence.
            JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${SAP_TARGET}
        PRIVATE
            SattariAudioCore
            juce::juce_audio_utils
            juce::juce_dsp            # Oversampling, FFT, etc. (suite-wide DSP toolkit)
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()
