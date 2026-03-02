macro(Build3rdParty)
    #list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/FindPackage/SDL2)
    #GetPackageInstallDir(SDL2_INSTALL_DIR sdl_build ${sdl_VERSION})
    #Find_Package(SDL2 REQUIRED)
    #BuildExternalCMake(sdl_build ${sdl_VERSION} ${sdl_SOURCE_DIR} "" "SDL2::SDL2;SDL2::SDL2main")
    # Save and strip -fno-exceptions/-fno-rtti for third-party subdirectories
    # that need exceptions (vio tests, fmt, laszip, etc). Restore after.
    set(_SAVED_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # MSVC equivalents: restore /EHsc (exceptions) and /GR (RTTI), remove _HAS_EXCEPTIONS=0
    string(REPLACE "/EHs-c-" "/EHsc" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/GR-" "/GR" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-D_HAS_EXCEPTIONS=0" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    if (POINTS_BUILD_EXAMPLES)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        add_subdirectory(${sdl_SOURCE_DIR} SYSTEM)
        unset(SDL_TEST_LIBRARY CACHE)
    endif ()

    set(VIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(VIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # vio builds libuv as static (LIBUV_BUILD_SHARED OFF) but libuv's alias
    # target selects uv vs uv_a based on BUILD_SHARED_LIBS. Override it so
    # the alias points to the static target that actually gets built.
    set(_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(${vio_SOURCE_DIR} SYSTEM)
    set(BUILD_SHARED_LIBS ${_SAVED_BUILD_SHARED_LIBS})
    unset(VIO_BUILD_TESTS CACHE)
    unset(VIO_BUILD_EXAMPLES CACHE)
    # vio changed its include directory from PUBLIC to PRIVATE; re-expose it
    target_include_directories(vio PUBLIC ${vio_SOURCE_DIR}/src)

    # Patch laszip 3.5.0 source files (only once after fresh extraction).
    # Guard with a stamp file to avoid re-patching (and corrupting) on
    # subsequent configure runs.
    set(_laszip_patch_stamp "${laszip_SOURCE_DIR}/.points_patched")
    if (NOT EXISTS "${_laszip_patch_stamp}")
        # Remove add_compile_options(-std=c++17) which wrongly applies C++
        # flags to C files.  CMAKE_CXX_STANDARD 17 is already set.
        file(READ "${laszip_SOURCE_DIR}/CMakeLists.txt" _laszip_cml)
        string(REPLACE "add_compile_options(\"/std:c++17\")" "# patched out: add_compile_options(\"/std:c++17\")" _laszip_cml "${_laszip_cml}")
        string(REPLACE "add_compile_options(-std=c++17)" "# patched out: add_compile_options(-std=c++17)" _laszip_cml "${_laszip_cml}")
        file(WRITE "${laszip_SOURCE_DIR}/CMakeLists.txt" "${_laszip_cml}")
        # LASZIP_ADD_LIBRARY sets CXX_STANDARD 11 on targets, overriding the
        # global CMAKE_CXX_STANDARD 17.  Fix to use 17.
        file(READ "${laszip_SOURCE_DIR}/cmake/macros.cmake" _laszip_macros)
        string(REPLACE "CXX_STANDARD 11" "CXX_STANDARD 17" _laszip_macros "${_laszip_macros}")
        file(WRITE "${laszip_SOURCE_DIR}/cmake/macros.cmake" "${_laszip_macros}")
        file(WRITE "${_laszip_patch_stamp}" "")
    endif ()
    set(LASZIP_BUILD_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory(${laszip_SOURCE_DIR} ${CMAKE_BINARY_DIR}/laszip_build SYSTEM)
    unset(LASZIP_BUILD_STATIC CACHE)
    # The base laszip target is "laszip" on Linux, "laszip3" on Windows.
    # Expose it via POINTS_LASZIP_TARGET so consumers use it directly.
    set(POINTS_LASZIP_TARGET laszip)
    if(WIN32)
        set(POINTS_LASZIP_TARGET "laszip3")
    endif()
    target_include_directories(${POINTS_LASZIP_TARGET} PUBLIC
        ${laszip_SOURCE_DIR}/dll
        ${laszip_SOURCE_DIR}/include/laszip
        ${CMAKE_BINARY_DIR}/laszip_build/include/laszip)

    set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(${fmt_SOURCE_DIR} SYSTEM)
    # Clang on MinGW doesn't properly auto-detect -fno-exceptions for fmt headers
    if (NOT MSVC)
        target_compile_definitions(fmt PUBLIC FMT_EXCEPTIONS=0)
    endif ()
    add_subdirectory(${glm_SOURCE_DIR} SYSTEM)
    add_subdirectory(${catch2_SOURCE_DIR} SYSTEM)
    add_subdirectory(${unordered_dense_SOURCE_DIR} SYSTEM)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
    set(BLOSC_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory(${blosc2_SOURCE_DIR} SYSTEM SYSTEM)
    unset(BUILD_TESTS CACHE)
    unset(BUILD_BENCHMARKS CACHE)
    unset(BUILD_EXAMPLES CACHE)
    unset(BUILD_FUZZERS CACHE)
    unset(BLOSC_INSTALL CACHE)
    unset(BUILD_SHARED CACHE)
    unset(BUILD_STATIC CACHE)
    set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
    include(${cmakerc_SOURCE_DIR}/CMakeRC.cmake)

    # Restore original flags for points' own code
    set(CMAKE_CXX_FLAGS ${_SAVED_CMAKE_CXX_FLAGS})
endmacro()
