# The generated C++ wrappers: header-only, so this is an INTERFACE target that carries include paths
# and nothing else. Two headers, deliberately separate:
#
#   dew/dewpp.hpp   the whole public C API as C++ (RAII handles, std::expected errors). No vio.
#   dew/await.hpp   the vio coroutine driver for the awaitable handles. Needs vio.
#
# Keeping dewpp.hpp free of vio is what lets a consumer that has its own event loop -- or none -- use
# the C++ API without taking a dependency it does not want.
#
# They sit in bindings/ rather than src/ because that is what they are -- CONSUMERS of the public C
# surface, like bindings/python, not part of it. Both are generated from the bindgen IR
# (generate_cpp.py and generate_cpp_await.py) and CHECKED IN, because generating them needs libclang
# and an ordinary C++ build must not. tools/bindgen/test_ir.py fails if either goes stale.
#
# Linking this gives a consumer the include path for the header AND for the dew_* C headers it
# includes; the dew libraries themselves and vio are still the consumer's to link, which is the point
# -- the host owns the event loop.
add_library(dew_await INTERFACE)
add_library(dew::await ALIAS dew_await)

target_sources(dew_await INTERFACE FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/dew/dewpp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dew/await.hpp)

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

install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/dew/dewpp.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/dew/await.hpp
        DESTINATION include/dew COMPONENT dew_devel)
