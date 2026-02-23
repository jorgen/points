function(set_tool_properties target)
    if (APPLE)
        set_target_properties(${target} PROPERTIES
                BUILD_RPATH "@executable_path/../lib"
        )
    endif ()
endfunction()

add_subdirectory(jlp_info)
add_subdirectory(jlp_extract)
