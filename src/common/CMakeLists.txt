
set(public_headers
        dew/common/error.h
        dew/common/export.h
        dew/common/format.h
)
set(private_headers
        error.hpp
        fixed_size_vector.hpp
)
set(sources
        error.cpp
)

add_library(dew_common_objects OBJECT ${public_headers} ${private_headers} ${sources})
add_library(dew_common empty_dylib_function.cpp $<TARGET_OBJECTS:dew_common_objects>)
CmDepTargetLinkLibrary(dew_common PUBLIC dew_common_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(dew_common_objects PRIVATE DEW_COMMON_EXPORTS)
endif ()
set_target_properties(dew_common_objects
        PROPERTIES
        PUBLIC_HEADER "${public_headers}"
)
target_include_directories(dew_common_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
