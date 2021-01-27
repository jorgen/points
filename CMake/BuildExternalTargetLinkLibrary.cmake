function(BuildExternalTargetLinkLibrary target scope)
  foreach(build_target ${ARGN})
    if (TARGET ${build_target})
  	  get_target_property(target_type ${build_target} TYPE)
      if (target_type STREQUAL INTERFACE_LIBRARY)
        target_link_libraries(${target} ${scope} ${build_target})
      else()
        get_target_property(is_build_external ${build_target} BUILD_EXTERNAL)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_EXTERNAL_SUB_TARGETS "${build_target}")
        if (is_build_external)
          if (WIN32 AND (target_type STREQUAL SHARED_LIBRARY))
              get_target_property(imported_implib_debug ${build_target} BUILD_EXTERNAL_IMPORTED_IMPLIB_DEBUG)
              target_link_libraries(${target} ${scope} debug ${imported_implib_debug})
              get_target_property(imported_implib_release ${build_target} BUILD_EXTERNAL_IMPORTED_IMPLIB_RELEASE)
              target_link_libraries(${target} ${scope} optimized ${imported_implib_release})
	      else()
              get_target_property(imported_location_debug ${build_target} BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG)
              target_link_libraries(${target} ${scope} debug ${imported_location_debug})
              get_target_property(imported_location_release ${build_target} BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE)
              target_link_libraries(${target} ${scope} optimized ${imported_location_release})
          endif()
          get_target_property(imported_includes ${build_target} BUILD_EXTERNAL_INTERFACE_INCLUDE_DIRECTORIES)
          if (imported_includes)
            foreach(imported_include ${imported_includes})
              target_include_directories(${target} SYSTEM ${scope} ${imported_include})
            endforeach()
          endif()
          get_target_property(imported_compile_definitions ${build_target} BUILD_EXTERNAL_INTERFACE_COMPILE_DEFINITIONS)
          if (imported_compile_definitions)
            foreach(imported_compile_definition ${imported_compile_definitions})
              target_compile_definitions(${target} ${scope} ${imported_compile_definition})
            endforeach()
          endif()
          get_target_property(build_dep_target ${build_target} BUILD_EXTERNAL_TARGET)
          if (TARGET ${build_dep_target})
            add_dependencies(${target} ${build_dep_target})
          endif()
        else()
          target_link_libraries(${target} ${scope} ${build_target})
        endif()
      endif()
    endif()
  endforeach()
endfunction()
