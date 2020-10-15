function(copyDllForTarget target)
  if (WIN32)
    foreach(t ${ARGN})
      list(APPEND generator "$<TARGET_FILE:${t}>")
    endforeach()
    add_custom_command(OUTPUT "${target}_copy_deps"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:points_render> ${generator} $<TARGET_FILE_DIR:${target}>
      DEPENDS points_render ${ARGN}
      )
    set_property(SOURCE "${target}_copy_deps"
      PROPERTY SYMBOLIC ON
      )
    target_sources(${target} PRIVATE ${target}_copy_deps)
  endif()
endfunction()
