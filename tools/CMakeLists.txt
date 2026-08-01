function(set_tool_properties target)
    if (APPLE)
        set_target_properties(${target} PROPERTIES
                BUILD_RPATH "@executable_path/../lib"
        )
    endif ()
endfunction()

add_subdirectory(dew_info)
add_subdirectory(dew_extract)
add_subdirectory(dew_copy)
