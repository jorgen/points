include(BuildExternal)
include(GetPackageInstallDir)

macro(Build3rdParty)
  list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/FindPackage/SDL2)
  GetPackageInstallDir(SDL2_INSTALL_DIR sdl_build ${sdl_VERSION})
  Find_Package(SDL2 REQUIRED)
  BuildExternalCMake(sdl_build ${sdl_VERSION} ${sdl_SOURCE_DIR} "" "SDL2::SDL2;SDL2::SDL2main")
  
#  BuildExternalCMake(libuv_build ${libuv_VERSION} ${libuv_SOURCE_DIR} LIBUV_ROOT "")
#  message("curl ${curl_VERSION} ${curl_SOURCE_DIR}")
#  BuildExternalCMake(curl_build ${curl_VERSION} ${curl_SOURCE_DIR} CURL_ROOT "")
  set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${fmt_SOURCE_DIR})
  add_subdirectory(${glm_SOURCE_DIR})
  add_subdirectory(${catch2_SOURCE_DIR})
  set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
  include(${cmakerc_SOURCE_DIR}/CMakeRC.cmake)
  
  add_custom_target(build_3rd_party
    DEPENDS
      sdl_build
      #libuv_build
      #curl_build
  )

endmacro()
