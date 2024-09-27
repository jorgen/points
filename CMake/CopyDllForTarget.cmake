include(CMakePrintHelpers)

function(get_dll_filenames target release_dll debug_dll)
    set(release_dll_local "")
    set(debug_dll_local "")
    set(sub_release_dll_local "")
    set(sub_release_dll_local "")
    foreach (copy_target ${ARGN})
        if (TARGET ${copy_target})
            get_target_property(sub_targets ${copy_target} BUILD_EXTERNAL_SUB_TARGETS)
            if (sub_targets)
                get_dll_filenames(${copy_target} sub_release_dll_local sub_debug_dll_local ${sub_targets})
            endif ()
            if (sub_release_dll_local)
                list(APPEND release_dll_local ${sub_release_dll_local})
            endif ()
            if (sub_debug_dll_local)
                list(APPEND debug_dll_local ${sub_debug_dll_local})
            endif ()

            get_target_property(target_type ${copy_target} TYPE)
            if (target_type STREQUAL SHARED_LIBRARY)
                get_target_property(target_imported ${copy_target} IMPORTED)
                if (target_imported)
                    get_target_property(is_build_external ${copy_target} BUILD_EXTERNAL)
                    if (is_build_external)
                        get_target_property(loc_release ${copy_target} BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE)
                        get_target_property(loc_debug ${copy_target} BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG)
                        if (loc_release)
                            list(APPEND release_dll_local ${loc_release})
                        endif ()
                        if (loc_debug)
                            list(APPEND debug_dll_local ${loc_debug})
                        endif ()
                    endif ()
                else ()
                    list(APPEND release_dll_local "$<TARGET_FILE:${copy_target}>")
                    list(APPEND debug_dll_local "$<TARGET_FILE:${copy_target}>")
                endif ()
            endif ()
        endif ()
    endforeach ()
    if (release_dll_local)
        set(${release_dll} "${release_dll_local}" PARENT_SCOPE)
    endif ()
    if (debug_dll_local)
        set(${debug_dll} "${debug_dll_local}" PARENT_SCOPE)
    endif ()
endfunction()


function(copy_dll_for_target target)
    if (WIN32 OR APPLE)
        message("Copy DLL for target ${target}")
        set(release_dll "")
        set(debug_dll "")
        get_dll_filenames(${target} "release_dll" "debug_dll" ${ARGN})
        message("Release DLL: ${release_dll}")
        message("Debug DLL: ${debug_dll}")
        foreach (dll ${release_dll})
            list(APPEND release_dlls "${dll}")
        endforeach ()
        foreach (dll ${debug_dll})
            list(APPEND debug_dlls "${dll}")
        endforeach ()

        if (release_dll OR debug_dll)
            if (WIN32)
                add_custom_command(OUTPUT ${target}_copy_runtime
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<$<NOT:$<CONFIG:Debug>>:${release_dlls}>" "$<$<CONFIG:Debug>:${debug_dlls}>" "$<SHELL_PATH:$<TARGET_FILE_DIR:${target}>>"
                        COMMAND_EXPAND_LISTS
                )
            else ()
                add_custom_command(OUTPUT ${target}_copy_runtime
                        COMMAND ${CMAKE_COMMAND} -E make_directory "$<SHELL_PATH:$<TARGET_FILE_DIR:${target}>/../lib>"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<$<NOT:$<CONFIG:Debug>>:${release_dlls}>" "$<$<CONFIG:Debug>:${debug_dlls}>" "$<SHELL_PATH:$<TARGET_FILE_DIR:${target}>/../lib>"
                        COMMAND_EXPAND_LISTS
                )
            endif ()
            set_property(SOURCE "${target}_copy_runtime"
                    PROPERTY SYMBOLIC ON)
            target_sources(${target} PRIVATE ${target}_copy_runtime)
        endif ()

    endif ()
endfunction()
