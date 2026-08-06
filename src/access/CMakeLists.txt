
set(public_headers
        dew/access/export.h
        dew/access/query.h
)
set(private_headers
        region_walk.hpp
        dataset_impl.hpp
        decode.hpp
)
set(sources
        region_walk.cpp
        dataset.cpp
        request.cpp
        query_api.cpp
        decode.cpp
)

add_library(dew_access_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(dew_access_objects PRIVATE fmt glm unordered_dense dew_core vio_objstore libzstd_static)
target_include_directories(dew_access_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(dew_access_objects
        PRIVATE
        ${libmorton_SOURCE_DIR}/include
        ${zstd_SOURCE_DIR}/lib
)
setWarningflagsForTarget(dew_access_objects)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(dew_access_objects PRIVATE DEW_ACCESS_EXPORTS)
endif ()

# dew_core's objects are spliced in, not linked: its cross-module API is internal C++ and the build
# uses hidden visibility, so those symbols are not exported from libdew_core (see src/converter).
add_library(dew_access empty_dylib_function.cpp $<TARGET_OBJECTS:dew_access_objects> $<TARGET_OBJECTS:dew_core_objects>)
target_link_libraries(dew_access PUBLIC dew_access_objects)
