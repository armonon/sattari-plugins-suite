cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/SattariDeploymentTarget.cmake)
project(SATTARI_MODULAR VERSION 0.1.1)
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
        VERSION 0.1.1
        ${extra} NEEDS_MIDI_INPUT EDITOR_WANTS_KEYBOARD_FOCUS
        SOURCES src/PluginProcessor.cpp src/PluginEditor.cpp)
    target_compile_definitions(SattariModular${kind} PUBLIC SATTARI_MODULAR_SYNTH=${synth})
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
    add_test(NAME ModularFX COMMAND SattariModularFXContract ${CMAKE_CURRENT_BINARY_DIR}/modular-verification/FX)
    add_test(NAME ModularInstrument COMMAND SattariModularInstrumentContract ${CMAKE_CURRENT_BINARY_DIR}/modular-verification/Instrument)
    set_tests_properties(ModularFX ModularInstrument PROPERTIES TIMEOUT 300)
endif()
add_custom_target(SattariModularBuild DEPENDS
    SattariModularFX_All SattariModularInstrument_All
    SattariModularFXContract SattariModularInstrumentContract)
