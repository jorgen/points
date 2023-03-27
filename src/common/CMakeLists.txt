include(BuildExternalTargetLinkLibrary)

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

add_library(common_objects OBJECT ${public_headers} ${private_headers} ${sources})
add_library(common empty_dylib_function.cpp $<TARGET_OBJECTS:common_objects>)
BuildExternalTargetLinkLibrary(common PUBLIC common_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(common_objects PRIVATE POINTS_COMMON_EXPORTS)
endif()
set_target_properties(common_objects
  PROPERTIES
  PUBLIC_HEADER "${public_headers}"
)
target_include_directories(common_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
