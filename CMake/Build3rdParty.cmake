function(Build3rdParty)
  set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${fmt_SOURCE_DIR})
  add_subdirectory(${sdl_SOURCE_DIR})
  add_subdirectory(${glm_SOURCE_DIR})
  add_subdirectory(${catch2_SOURCE_DIR})
  set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
endfunction()
