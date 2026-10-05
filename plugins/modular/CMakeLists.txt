cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MODULAR VERSION 0.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariPlugin.cmake)
option(SATTARI_MODULAR_LTO "Enable whole-program optimization for Modular" ON)

foreach(kind IN ITEMS FX Instrument)
    set(extra)
    if(kind STREQUAL Instrument)
        set(extra IS_SYNTH)
        set(code MoI1)
        set(synth 1)
    else()
        set(extra MICROPHONE_PERMISSION_TEXT "Process audio from your selected microphone or audio interface input.")
        set(code MoF1)
        set(synth 0)
    endif()
    sattari_add_plugin(TARGET SattariModular${kind}
        SLUG modular PRODUCT_NAME "Sattari Modular ${kind}" PLUGIN_CODE ${code}
        VERSION 0.2.0
        ${extra} NEEDS_MIDI_INPUT EDITOR_WANTS_KEYBOARD_FOCUS
        SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp src/HostedPlugins.cpp)
    target_compile_definitions(SattariModular${kind} PUBLIC SATTARI_MODULAR_SYNTH=${synth} JUCE_PLUGINHOST_VST3=1)
    if(APPLE AND TARGET SattariModular${kind}_AU)
        add_custom_command(TARGET SattariModular${kind}_AU POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign -
                "$<TARGET_BUNDLE_DIR:SattariModular${kind}_AU>"
            COMMENT "Ad-hoc signing Modular ${kind} Audio Unit" VERBATIM)
    endif()
    if(NOT SATTARI_MODULAR_LTO)
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(links SattariModular${kind} ${property})
            list(REMOVE_ITEM links juce::juce_recommended_lto_flags)
            set_property(TARGET SattariModular${kind} PROPERTY ${property} "${links}")
        endforeach()
    endif()
    add_executable(SattariModular${kind}Contract EXCLUDE_FROM_ALL tests/modular_contract.cpp)
    target_include_directories(SattariModular${kind}Contract PRIVATE src
        ../../core/include ../../core/tests
        ${CMAKE_CURRENT_BINARY_DIR}/SattariModular${kind}_artefacts/JuceLibraryCode)
    target_include_directories(SattariModular${kind}Contract PRIVATE
        $<TARGET_PROPERTY:SattariModular${kind},INCLUDE_DIRECTORIES>)
    target_compile_definitions(SattariModular${kind}Contract PRIVATE
        $<TARGET_PROPERTY:SattariModular${kind},COMPILE_DEFINITIONS>)
    target_link_libraries(SattariModular${kind}Contract PRIVATE SattariModular${kind})
endforeach()
include(CTest)
if(BUILD_TESTING)
    foreach(kind IN ITEMS Effect Instrument)
        if(kind STREQUAL Instrument)
            set(fixtureSynth TRUE)
            set(fixtureCode HtI1)
        else()
            set(fixtureSynth FALSE)
            set(fixtureCode HtE1)
        endif()
        juce_add_plugin(ModularHosting${kind}Fixture
            COMPANY_NAME "Sattari Tests" PLUGIN_MANUFACTURER_CODE SatT PLUGIN_CODE ${fixtureCode}
            BUNDLE_ID "com.sattari.tests.hosting${kind}"
            PRODUCT_NAME "Modular Hosting ${kind} Fixture" FORMATS VST3
            IS_SYNTH ${fixtureSynth} NEEDS_MIDI_INPUT TRUE COPY_PLUGIN_AFTER_BUILD FALSE)
        juce_generate_juce_header(ModularHosting${kind}Fixture)
        target_sources(ModularHosting${kind}Fixture PRIVATE tests/hosting_fixture.cpp)
        target_compile_definitions(ModularHosting${kind}Fixture PUBLIC
            FIXTURE_SYNTH=$<BOOL:${fixtureSynth}> JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        target_link_libraries(ModularHosting${kind}Fixture PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
    endforeach()
    foreach(kind IN ITEMS FX Instrument)
        add_executable(SattariModular${kind}HostingContract EXCLUDE_FROM_ALL tests/hosting_contract.cpp)
        target_include_directories(SattariModular${kind}HostingContract PRIVATE src ../../core/include
            ${CMAKE_CURRENT_BINARY_DIR}/SattariModular${kind}_artefacts/JuceLibraryCode
            $<TARGET_PROPERTY:SattariModular${kind},INCLUDE_DIRECTORIES>)
        target_compile_definitions(SattariModular${kind}HostingContract PRIVATE
            $<TARGET_PROPERTY:SattariModular${kind},COMPILE_DEFINITIONS>)
        target_link_libraries(SattariModular${kind}HostingContract PRIVATE SattariModular${kind})
        add_dependencies(SattariModular${kind}HostingContract ModularHostingEffectFixture_VST3 ModularHostingInstrumentFixture_VST3)
        add_test(NAME Modular${kind}Hosting COMMAND SattariModular${kind}HostingContract
            "$<GENEX_EVAL:$<TARGET_PROPERTY:ModularHostingEffectFixture_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
            "$<GENEX_EVAL:$<TARGET_PROPERTY:ModularHostingInstrumentFixture_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
            "${CMAKE_CURRENT_BINARY_DIR}/modular-verification/${kind}/hosting")
        set_tests_properties(Modular${kind}Hosting PROPERTIES TIMEOUT 300 LABELS "product;modular;hosting;non-stemdeck")
    endforeach()
    add_test(NAME ModularFX COMMAND SattariModularFXContract ${CMAKE_CURRENT_BINARY_DIR}/modular-verification/FX)
    add_test(NAME ModularInstrument COMMAND SattariModularInstrumentContract ${CMAKE_CURRENT_BINARY_DIR}/modular-verification/Instrument)
    set_tests_properties(ModularFX ModularInstrument PROPERTIES TIMEOUT 300 LABELS "product;modular;non-stemdeck")
endif()
add_custom_target(SattariModularBuild DEPENDS
    SattariModularFX_All SattariModularInstrument_All
    SattariModularFXContract SattariModularInstrumentContract)
if(BUILD_TESTING)
    add_dependencies(SattariModularBuild SattariModularFXHostingContract SattariModularInstrumentHostingContract)
endif()
