# The generated C++ wrappers: header-only, so this is an INTERFACE target that carries include paths
# and nothing else. Two headers, deliberately separate:
#
#   dew/**/*.hpp    the public C API as C++ (RAII handles, std::expected errors). No vio. The tree
#                   MIRRORS the C headers, so <dew/access/query.hpp> is the C++ for
#                   <dew/access/query.h>; dew/dewpp.hpp is an umbrella over all of them.
#   dew/**/*_async.hpp  co_await siblings for the pump-driven handles -- dew/access/query_async.hpp
#                   beside dew/access/query.hpp, with the driver in dew/core/pump_async.hpp. This is
#                   the only part that needs vio, which is why it is separable: a caller with no
#                   event loop includes the sync headers and never pays for it.
#
# Keeping dewpp.hpp free of vio is what lets a consumer that has its own event loop -- or none -- use
# the C++ API without taking a dependency it does not want.
#
# They sit in bindings/ rather than src/ because that is what they are -- CONSUMERS of the public C
# surface, like bindings/python, not part of it. All of it is generated from the bindgen IR
# (generate_cpp.py and generate_cpp_await.py) and CHECKED IN, because generating it needs libclang
# and an ordinary C++ build must not. tools/bindgen/test_ir.py fails if any of it goes stale.
#
# Linking this gives a consumer the include path for the header AND for the dew_* C headers it
# includes; the dew libraries themselves and vio are still the consumer's to link, which is the point
# -- the host owns the event loop.
add_library(dew_await INTERFACE)
add_library(dew::await ALIAS dew_await)

# Globbed rather than listed: the set is whatever generate_cpp.py emitted, and a stale hand-written
# list would be one more thing to drift. The bindgen test is what notices a missing or extra file.
file(GLOB_RECURSE _dew_cpp_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dew/*.hpp)
target_sources(dew_await INTERFACE FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_dew_cpp_headers})

target_include_directories(dew_await INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # Every module's public C headers: dewpp.hpp wraps the whole API, so it includes all four.
        # Reached the same way a consumer of the installed package would.
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/access>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/render>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/converter>
        $<INSTALL_INTERFACE:include>
)

# DIRECTORY, so the mirrored dew/core, dew/access, dew/render and dew/converter subtrees land in the
# same shape a consumer includes them in.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dew DESTINATION include COMPONENT dew_devel FILES_MATCHING PATTERN "*.hpp")
