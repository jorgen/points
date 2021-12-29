set(LASZIP_API_INCLUDE_DIRECTORIES ${LASZIP_INSTALL_DIR}/include)

if (WIN32)
  set(LASZIP_API_IMPL_LIB_RELEASE ${LASZIP_INSTALL_DIR}/lib/laszip3.lib)
  set(LASZIP_API_IMPL_LIB_DEBUG ${LASZIP_INSTALL_DIR}/lib/laszip3d.lib)

  set(LASZIP_API_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/bin/laszip_api3.dll)
  set(LASZIP_API_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/bin/laszip_api3d.dll)
  set(LASZIP_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/bin/laszip3.dll)
  set(LASZIP_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/bin/laszip3d.dll)

elseif(APPLE)
  set(LASZIP_API_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/lib/liblaszip_api.8.0.5.dylib)
  set(LASZIP_API_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/lib/liblaszip_api.8.0.5.dylib)
  set(LASZIP_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/lib/liblaszip.8.0.5.dylib)
  set(LASZIP_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/lib/liblaszip.8.0.5.dylib)
else()
  set(LASZIP_API_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/bin/laszip_api3.dll)
  set(LASZIP_API_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/bin/laszip_api3d.dll)
  set(LASZIP_LOCATION_RELEASE ${LASZIP_INSTALL_DIR}/bin/laszip3.dll)
  set(LASZIP_LOCATION_DEBUG ${LASZIP_INSTALL_DIR}/bin/laszip3d.dll)
endif()

if (NOT TARGET laszip::api)
  add_library(laszip::api SHARED IMPORTED)
  set_target_properties(laszip::api
    PROPERTIES
    BUILD_EXTERNAL "On"
    BUILD_EXTERNAL_TARGET laszip_build
    BUILD_EXTERNAL_INTERFACE_INCLUDE_DIRECTORIES      "${LASZIP_API_INCLUDE_DIRECTORIES}"
    BUILD_EXTERNAL_INTERFACE_COMPILE_DEFINITIONS      "LASZIP_DYN_LINK=1"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_RELEASE            "${LASZIP_API_IMPL_LIB_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_DEBUG              "${LASZIP_API_IMPL_LIB_DEBUG}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE          "${LASZIP_API_LOCATION_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG            "${LASZIP_API_LOCATION_DEBUG}"
    BUILD_EXTERNAL_SUB_TARGETS         "laszip::impl"
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  )
endif()

if (NOT TARGET laszip::impl)
  add_library(laszip::impl SHARED IMPORTED)
  set_target_properties(laszip::impl
    PROPERTIES
    BUILD_EXTERNAL "On"
    BUILD_EXTERNAL_TARGET laszip_build
    BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE          "${LASZIP_LOCATION_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG            "${LASZIP_LOCATION_DEBUG}"
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  )
endif()
