include(GetPackageInstallDir)
function(BuildExternalCMake name version source_dir cmake_args build_targets)
  GetPackageInstallDir(INSTALL_INT_CONFIG ${name} ${version})
  set(CMAKE_BUILD_TYPE_ARG "-DCMAKE_BUILD_TYPE=$<$<NOT:$<CONFIG:Debug>>:Release>$<$<CONFIG:Debug>:Debug>;")

  if (cmake_args)
    set(cmake_arg_complete "${cmake_args};")
  endif()
  set(cmake_arg_complete "${cmake_arg_complete}${CMAKE_BUILD_TYPE_ARG}")

  if (CCACHE_PROGRAM)
    set(cmake_arg_complete "${cmake_arg_complete}-DCMAKE_C_COMPILER_LAUNCHER=ccache;-DCMAKE_CXX_COMPILER_LAUNCHER=ccache;")
  endif()

  set(cmake_arg_complete "${cmake_arg_complete}-DCMAKE_INSTALL_PREFIX=${INSTALL_INT_CONFIG};-DCMAKE_INSTALL_MESSAGE=LAZY")
  set(cmake_arg_complete "${cmake_arg_complete}-DCMAKE_CXX_FLAGS:${CMAKE_CXX_FLAGS}")

  foreach(build_target ${build_targets})
    if (TARGET ${build_target})
	  get_target_property(target_type ${build_target} TYPE)
      if (WIN32 AND (target_type STREQUAL SHARED_LIBRARY))
        get_target_property(imported_implig_debug ${build_target} BUILD_EXTERNAL_IMPORTED_IMPLIB_DEBUG)
        list(APPEND build_byproducts ${imported_implig_debug})
        get_target_property(imported_implig_release ${build_target} BUILD_EXTERNAL_IMPORTED_IMPLIB_RELEASE)
        if (NOT "${imported_implig_release}" STREQUAL "${imported_implig_debug}")
          list(APPEND build_byproducts ${imported_implig_release})
        endif()
      else()
        get_target_property(imported_location_debug ${build_target} BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG)
        list(APPEND build_byproducts ${imported_location_debug})
        get_target_property(imported_location_release ${build_target} BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE)
        if (NOT "${imported_location_release}" STREQUAL "${imported_location_debug}")
          list(APPEND build_byproducts ${imported_location_release})
        endif()
      endif()
    endif()
  endforeach()

  include(ExternalProject)
  ExternalProject_Add(${name}
    PREFIX ${PROJECT_BINARY_DIR}/${name}_${version}
    SOURCE_DIR ${source_dir}
    BUILD_IN_SOURCE OFF
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    CMAKE_GENERATOR_PLATFORM ${CMAKE_GENERATOR_PLATFORM}
    CMAKE_ARGS ${cmake_arg_complete}
    BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<$<NOT:$<CONFIG:Debug>>:Release>$<$<CONFIG:Debug>:Debug>
    BUILD_BYPRODUCTS ${build_byproducts})
endfunction()
 
