set(LIBUV_INCLUDE_DIRECTORIES ${LIBUV_INSTALL_DIR}/include)

if (WIN32)
  set(LIBUV_IMPL_LIB_RELEASE ${LIBUV_INSTALL_DIR}/lib/Release/uv.lib)
  set(LIBUV_IMPL_LIB_DEBUG ${LIBUV_INSTALL_DIR}/lib/Debug/uv.lib)

  set(LIBUV_LOCATION_RELEASE ${LIBUV_INSTALL_DIR}/lib/Release/uv.dll)
  set(LIBUV_LOCATION_DEBUG ${LIBUV_INSTALL_DIR}/lib/Debug/uv.dll)
elseif(APPLE)
  set(LIBUV_LOCATION_RELEASE ${LIBUV_INSTALL_DIR}/lib/libuv.1.0.0.dylib)
  set(LIBUV_LOCATION_DEBUG ${LIBUV_INSTALL_DIR}/lib/libuv.1.0.0.dylib)
else()
  set(LIBUV_IMPL_LIB_RELEASE ${LIBUV_INSTALL_DIR}/lib/Release/uv.lib)
  set(LIBUV_IMPL_LIB_DEBUG ${LIBUV_INSTALL_DIR}/lib/Debug/uv.lib)

  set(LIBUV_LOCATION_RELEASE ${LIBUV_INSTALL_DIR}/lib/Release/uv.dll)
  set(LIBUV_LOCATION_DEBUG ${LIBUV_INSTALL_DIR}/lib/Debug/uv.dll)
endif()


if (NOT TARGET libuv::libuv)
  add_library(libuv::libuv SHARED IMPORTED)
  set_target_properties(libuv::libuv
    PROPERTIES
    BUILD_EXTERNAL "On"
    BUILD_EXTERNAL_TARGET libuv_build
    BUILD_EXTERNAL_INTERFACE_INCLUDE_DIRECTORIES      "${LIBUV_INCLUDE_DIRECTORIES}"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_RELEASE            "${LIBUV_IMPL_LIB_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_DEBUG              "${LIBUV_IMPL_LIB_DEBUG}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE          "${LIBUV_LOCATION_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG            "${LIBUV_LOCATION_DEBUG}"
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  )
endif()

