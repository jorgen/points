# The generated C++ await wrappers: header-only, so this is an INTERFACE target that carries include
# paths and nothing else.
#
# It sits in bindings/ rather than src/ because that is what it is -- a CONSUMER of the public C
# surface, like bindings/python, not part of it. dew/await.hpp is generated from the `//= awaitable:`
# annotations by tools/bindgen/generate_cpp_await.py and CHECKED IN, because generating it needs
# libclang and an ordinary C++ build must not. tools/bindgen/test_ir.py fails if it goes stale.
#
# Linking this gives a consumer the include path for the header AND for the dew_* C headers it
# includes; the dew libraries themselves and vio are still the consumer's to link, which is the point
# -- the host owns the event loop.
add_library(dew_await INTERFACE)
add_library(dew::await ALIAS dew_await)

target_sources(dew_await INTERFACE FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CMAKE_CURRENT_SOURCE_DIR}/dew/await.hpp)

target_include_directories(dew_await INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # The public C headers it includes (dew/access/query.h, dew/core/pump.h), reached the same way
        # a consumer of the installed package would.
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/access>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/core>
        $<INSTALL_INTERFACE:include>
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/dew/await.hpp DESTINATION include/dew COMPONENT dew_devel)
