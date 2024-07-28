include(BuildExternalTargetLinkLibrary)

Find_Package(libuv REQUIRED)
Find_Package(CURL REQUIRED)

set(public_headers
        points/render/export.h
        points/render/camera.h
        points/render/renderer.h
        points/render/aabb.h
        points/render/aabb_data_source.h
        points/render/flat_points_data_source.h)

set(private_headers
        camera.hpp
        aabb.hpp
        data_source_aabb.hpp
        frustum.hpp
        glm_include.hpp
        data_source.hpp
        buffer.hpp
        image_decoder.hpp
        data_source_flat_points.hpp)

set(sources
        renderer.cpp
        camera.cpp
        data_source_aabb.cpp
        data_source_skybox.cpp
        data_source.cpp
        buffer.cpp
        image_decoder.cpp
        data_source_flat_points.cpp
)
add_library(points_render_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(points_render_objects PRIVATE fmt glm)
target_link_libraries(points_render_objects PUBLIC points_common)
BuildExternalTargetLinkLibrary(points_render_objects PRIVATE libuv::libuv CURL::libcurl laszip::api)
target_include_directories(points_render_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(points_render_objects SYSTEM PRIVATE ${stbimage_SOURCE_DIR})
#target_include_directories(points_render_objects PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
setWarningflagsForTarget(points_render_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(points_render_objects PRIVATE POINTS_RENDER_EXPORTS)
endif ()

add_library(points_render empty_dylib_function.cpp $<TARGET_OBJECTS:points_render_objects>)
BuildExternalTargetLinkLibrary(points_render PUBLIC points_render_objects)
