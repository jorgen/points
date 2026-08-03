# Python bindings: public headers -> JSON IR -> nanobind C++ -> `import dew`.
#
# The two generator stages (tools/bindgen/parse_headers.py + generate_nanobind.py)
# run at build time; nothing generated is committed. Requires a python3 with the
# `libclang` package (pip install libclang) -- point CMake at it with
# -DPython_EXECUTABLE=/path/to/python when it is not the default interpreter.

# nanobind needs RTTI and exceptions, which the project disables globally.
# Restore them at directory scope (same idiom as src/wasm and Build3rdParty);
# this also covers libnanobind and robin-map added below. Exceptions never
# cross into the dew libraries: wrappers throw only after the C call returns.
string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/EHs-c-" "/EHsc" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/GR-" "/GR" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-D_HAS_EXCEPTIONS=0" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import clang.cindex"
    RESULT_VARIABLE _dew_have_libclang
    OUTPUT_QUIET ERROR_QUIET)
if (NOT _dew_have_libclang EQUAL 0)
    message(FATAL_ERROR
        "DEW_BUILD_PYTHON: ${Python_EXECUTABLE} cannot import clang.cindex. "
        "Install it with: ${Python_EXECUTABLE} -m pip install libclang")
endif ()

# The fetched nanobind tarball lacks its ext/robin_map submodule; the separately
# fetched robin-map provides the tsl::robin_map target nanobind falls back to.
set(NB_USE_SUBMODULE_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory(${robin_map_SOURCE_DIR} robin_map SYSTEM)
add_subdirectory(${nanobind_SOURCE_DIR} nanobind SYSTEM)

set(_bindgen_dir ${CMAKE_SOURCE_DIR}/tools/bindgen)
set(_ir ${CMAKE_CURRENT_BINARY_DIR}/dew_api.json)
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/dew_bindings_generated.cpp)

# Every public header in the manifest is a dependency of the IR. The DEPENDS
# list is fixed at configure time, so the manifest itself is a configure
# dependency: adding a header to it re-runs CMake and picks the new file up.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_bindgen_dir}/public_headers.txt)
file(STRINGS ${_bindgen_dir}/public_headers.txt _dew_manifest_lines)
set(_dew_public_headers "")
foreach (_line IN LISTS _dew_manifest_lines)
    string(REGEX REPLACE "#.*" "" _line "${_line}")
    string(STRIP "${_line}" _line)
    # '!' lines are acknowledged exclusions, not inputs -- but they still name
    # real headers (export.h, transitively included), so track them too.
    string(REGEX REPLACE "^!" "" _line "${_line}")
    if (_line)
        list(APPEND _dew_public_headers ${CMAKE_SOURCE_DIR}/src/${_line})
    endif ()
endforeach ()

add_custom_command(OUTPUT ${_ir}
    COMMAND ${Python_EXECUTABLE} ${_bindgen_dir}/parse_headers.py
            --source-root ${CMAKE_SOURCE_DIR}/src
            --manifest ${_bindgen_dir}/public_headers.txt
            --output ${_ir}
    DEPENDS ${_dew_public_headers}
            ${_bindgen_dir}/public_headers.txt
            ${_bindgen_dir}/parse_headers.py
            ${_bindgen_dir}/clang_util.py
            ${_bindgen_dir}/semantics.py
            ${_bindgen_dir}/ir.py
    VERBATIM
    COMMENT "bindgen: public headers -> dew_api.json")

add_custom_command(OUTPUT ${_generated}
    COMMAND ${Python_EXECUTABLE} ${_bindgen_dir}/generate_nanobind.py
            --ir ${_ir} --output ${_generated}
    DEPENDS ${_ir} ${_bindgen_dir}/generate_nanobind.py
    VERBATIM
    COMMENT "bindgen: dew_api.json -> nanobind C++")

nanobind_add_module(dew_python NB_STATIC ${_generated} dew_py_support.cpp
    dew_py_support.h custom/file_convert_callbacks.h)
set_target_properties(dew_python PROPERTIES OUTPUT_NAME "dew")
target_include_directories(dew_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dew_python PRIVATE dew_converter dew_render dew_common)

if (APPLE)
    # belt-and-braces: CMake's automatic build rpath already points at the
    # dew_* build dirs; this helps if the module is copied next to the dylibs
    set_target_properties(dew_python PROPERTIES BUILD_RPATH "@loader_path")
endif ()
if (WIN32)
    copy_dll_for_target(dew_python dew_render dew_converter dew_common)
endif ()

if (DEW_BUILD_TESTS)
    # Always available (python + libclang are already required to build at all):
    # fails on an unclassifiable declaration, an unlisted public header, or a
    # change to the API-surface counts, so drift cannot pass unnoticed.
    add_test(NAME bindgen_check
        COMMAND ${Python_EXECUTABLE} ${_bindgen_dir}/parse_headers.py
                --source-root ${CMAKE_SOURCE_DIR}/src
                --manifest ${_bindgen_dir}/public_headers.txt
                --check)

    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import pytest, numpy"
        RESULT_VARIABLE _dew_have_pytest
        OUTPUT_QUIET ERROR_QUIET)
    if (_dew_have_pytest EQUAL 0)
        add_test(NAME python_bindings_smoke
            COMMAND ${Python_EXECUTABLE} -m pytest -q ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        set_tests_properties(python_bindings_smoke PROPERTIES
            ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:dew_python>")
        # The inference rules themselves (class grouping, roles, conventions).
        add_test(NAME bindgen_ir
            COMMAND ${Python_EXECUTABLE} -m pytest -q ${_bindgen_dir}/test_ir.py)
    else ()
        message(STATUS "dew python pytest suites disabled: ${Python_EXECUTABLE} lacks pytest and/or numpy "
                       "(bindgen_check still runs)")
    endif ()
endif ()
