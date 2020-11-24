set(public_headers
  points/render/camera.h
  points/render/renderer.h
  points/render/aabb.h "points/render/export.h")
set(private_headers
  camera_p.h
  aabb_p.h
  glm_include.h)
set(sources renderer.cpp camera.cpp)
add_library(points_render_objects OBJECT ${public_headers} ${private_headers} ${sources} "frustum_p.h")
target_link_libraries(points_render_objects PRIVATE fmt glm)
target_include_directories(points_render_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
#target_include_directories(points_render_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
setWarningflagsForTarget(points_render_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(points_render_objects PRIVATE points_render_EXPORTS)
endif()

add_library(points_render empty_dylib_function.cpp $<TARGET_OBJECTS:points_render_objects>)
target_link_libraries(points_render PUBLIC points_render_objects)
