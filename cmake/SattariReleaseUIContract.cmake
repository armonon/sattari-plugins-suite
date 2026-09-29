include_guard(GLOBAL)

function(sattari_add_release_ui_contract plugin processor slug)
    set(target "${plugin}UIContract")
    add_executable(${target} EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../core/tests/release_ui_contract.cpp")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        $<TARGET_PROPERTY:${plugin},INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE
        RELEASE_UI_PROCESSOR=${processor}
        RELEASE_UI_SLUG="${slug}"
        $<TARGET_PROPERTY:${plugin},COMPILE_DEFINITIONS>)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_link_libraries(${target} PRIVATE ${plugin})
endfunction()
