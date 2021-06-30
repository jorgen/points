set(SDL2_INCLUDE_DIRECTORIES ${SDL2_INSTALL_DIR}/include;${SDL2_INSTALL_DIR}/include/SDL2)

set(SDL2_IMPL_LIB_RELEASE ${SDL2_INSTALL_DIR}/lib/SDL2.lib)
set(SDL2_IMPL_LIB_DEBUG ${SDL2_INSTALL_DIR}/lib/SDL2d.lib)

set(SDL2_LOCATION_RELEASE ${SDL2_INSTALL_DIR}/bin/SDL2.dll)
set(SDL2_LOCATION_DEBUG ${SDL2_INSTALL_DIR}/bin/SDL2d.dll)

set(SDL2_MAIN_LOCATION_RELEASE ${SDL2_INSTALL_DIR}/lib/SDL2main.lib)
set(SDL2_MAIN_LOCATION_DEBUG ${SDL2_INSTALL_DIR}/lib/SDL2maind.lib)

if (NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 SHARED IMPORTED)
  set_target_properties(SDL2::SDL2
    PROPERTIES
    BUILD_EXTERNAL "On"
    BUILD_EXTERNAL_TARGET sdl_build
    BUILD_EXTERNAL_INTERFACE_INCLUDE_DIRECTORIES      "${SDL2_INCLUDE_DIRECTORIES}"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_RELEASE            "${SDL2_IMPL_LIB_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_IMPLIB_DEBUG              "${SDL2_IMPL_LIB_DEBUG}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE          "${SDL2_LOCATION_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG            "${SDL2_LOCATION_DEBUG}"
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  )
endif()

if (NOT TARGET SDL2::SDL2main)
  add_library(SDL2::SDL2main STATIC IMPORTED)
  set_target_properties(SDL2::SDL2main
    PROPERTIES
    BUILD_EXTERNAL "On"
    BUILD_EXTERNAL_TARGET sdl_build
    BUILD_EXTERNAL_INTERFACE_INCLUDE_DIRECTORIES      "${SDL2_INCLUDE_DIRECTORIES}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_RELEASE          "${SDL2_MAIN_LOCATION_RELEASE}"
    BUILD_EXTERNAL_IMPORTED_LOCATION_DEBUG            "${SDL2_MAIN_LOCATION_DEBUG}"
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
  )
endif()