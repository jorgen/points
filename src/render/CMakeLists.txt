
set(public_headers
        dew/render/export.h
        dew/render/camera.h
        dew/render/renderer.h
        dew/render/aabb.h
        dew/render/aabb_data_source.h
        dew/render/flat_points_data_source.h
        dew/render/axis_gizmo_data_source.h
        dew/render/origin_anchor_data_source.h
        dew/render/environment_data_source.h)

set(private_headers
        camera.hpp
        aabb.hpp
        data_source_aabb.hpp
        frustum.hpp
        glm_include.hpp
        data_source.hpp
        buffer.hpp
        image_decoder.hpp
        data_source_flat_points.hpp
        data_source_axis_gizmo.hpp
        data_source_origin_anchor.hpp
        data_source_environment.hpp
        node_data_loader.hpp)

set(sources
        renderer.cpp
        camera.cpp
        data_source_aabb.cpp
        data_source_skybox.cpp
        data_source.cpp
        buffer.cpp
        image_decoder.cpp
        data_source_flat_points.cpp
        data_source_axis_gizmo.cpp
        data_source_origin_anchor.cpp
        data_source_environment.cpp
)
add_library(dew_render_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(dew_render_objects PRIVATE fmt glm)
target_link_libraries(dew_render_objects PUBLIC dew_common)
target_link_libraries(dew_render_objects PRIVATE laszip)
target_include_directories(dew_render_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(dew_render_objects SYSTEM PRIVATE ${stbimage_SOURCE_DIR})
#target_include_directories(dew_render_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
setWarningflagsForTarget(dew_render_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(dew_render_objects PRIVATE DEW_RENDER_EXPORTS)
endif ()

add_library(dew_render empty_dylib_function.cpp $<TARGET_OBJECTS:dew_render_objects>)
target_link_libraries(dew_render PUBLIC dew_render_objects)
target_link_libraries(dew_render PRIVATE laszip)
