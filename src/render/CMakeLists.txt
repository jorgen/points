set(public_headers points/render/camera.h points/render/renderer.h)
set(sources renderer.cpp camera.cpp)
add_library(points_render SHARED ${public_headers} ${sources} "camera_p.h" "glm_include.h")
target_link_libraries(points_render PRIVATE fmt glm)
target_include_directories(points_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
#target_include_directories(points_render PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
setWarningflagsForTarget(points_render)
generate_export_header(points_render
    EXPORT_FILE_NAME points/render/points_render_export.h)
