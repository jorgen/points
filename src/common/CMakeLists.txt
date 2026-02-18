
set(public_headers
        points/common/error.h
        points/common/export.h
        points/common/format.h
)
set(private_headers
        error.hpp
        fixed_size_vector.hpp
)
set(sources
        error.cpp
)

add_library(points_common_objects OBJECT ${public_headers} ${private_headers} ${sources})
add_library(points_common empty_dylib_function.cpp $<TARGET_OBJECTS:points_common_objects>)
CmDepTargetLinkLibrary(points_common PUBLIC points_common_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(points_common_objects PRIVATE POINTS_COMMON_EXPORTS)
endif ()
set_target_properties(points_common_objects
        PROPERTIES
        PUBLIC_HEADER "${public_headers}"
)
target_include_directories(points_common_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
