
set(public_headers
        dew/core/error.h
        dew/core/export.h
        dew/core/format.h
)
set(private_headers
        error.hpp
        fixed_size_vector.hpp
)
set(sources
        error.cpp
)

add_library(dew_core_objects OBJECT ${public_headers} ${private_headers} ${sources})
add_library(dew_core empty_dylib_function.cpp $<TARGET_OBJECTS:dew_core_objects>)
CmDepTargetLinkLibrary(dew_core PUBLIC dew_core_objects)
if (BUILD_SHARED_LIBS)
    target_compile_definitions(dew_core_objects PRIVATE DEW_CORE_EXPORTS)
endif ()
set_target_properties(dew_core_objects
        PROPERTIES
        PUBLIC_HEADER "${public_headers}"
)
target_include_directories(dew_core_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
