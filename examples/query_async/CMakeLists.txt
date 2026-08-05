set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Splices the module objects rather than linking dew_access / dew_core, for the usual reason: the
# build sets CXX_VISIBILITY_PRESET=hidden, so only the public dew_* C entry points are exported from
# the shared libraries. This example uses nothing else -- it is a pure consumer of the public C API --
# but it does need vio's event loop, and vio comes in through the same objects.
add_executable(query_async
        query_async.cpp
        dew_vio_await.h
        $<TARGET_OBJECTS:dew_core_objects>
        $<TARGET_OBJECTS:dew_access_objects>
)

target_link_libraries(query_async PRIVATE vio_objstore libzstd_static)
target_link_libraries(query_async PRIVATE fmt unordered_dense)
target_include_directories(query_async PRIVATE
        ${PROJECT_SOURCE_DIR}/src/access
        ${PROJECT_SOURCE_DIR}/src/core
        ${libmorton_SOURCE_DIR}/include
)
copy_dll_for_target(query_async dew_access dew_core)
setWarningFlagsForTarget(query_async)
set_example_properties(query_async)
