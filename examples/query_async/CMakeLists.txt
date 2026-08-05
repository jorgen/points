set(CMAKE_INCLUDE_CURRENT_DIR ON)

# LINKS the libraries rather than splicing $<TARGET_OBJECTS:...>, and that is the point worth making.
#
# Everything else in this repo that consumes dew_core/dew_access -- tools/dew, the tests, the wasm
# modules -- splices objects, because it reaches into internal C++ (dew::core::blob_reader_t and
# friends) and the build sets CXX_VISIBILITY_PRESET=hidden, so those symbols are simply not in the
# shared library. Only the public dew_* C entry points are exported.
#
# This example touches nothing internal. If it needs anything beyond what libdew_access/libdew_core
# export, that is a hole in the public API, and the linker will say so. Keeping it an ordinary
# consumer is what makes it a real test of the surface rather than a demo with privileged access.
#
# vio is the exception, and legitimately so: the whole exercise is integrating the query API into a
# host that already owns an event loop, so the host brings its own vio.
add_executable(query_async
        query_async.cpp
        dew_vio_await.h
)

target_link_libraries(query_async PRIVATE dew_access dew_core)
target_link_libraries(query_async PRIVATE vio_objstore fmt)
target_include_directories(query_async PRIVATE
        ${PROJECT_SOURCE_DIR}/src/access/dew/..
        ${PROJECT_SOURCE_DIR}/src/core/dew/..
)
copy_dll_for_target(query_async dew_access dew_core)
setWarningFlagsForTarget(query_async)
set_example_properties(query_async)
