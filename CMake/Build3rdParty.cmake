macro(Build3rdParty)
    #list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/FindPackage/SDL2)
    #GetPackageInstallDir(SDL2_INSTALL_DIR sdl_build ${sdl_VERSION})
    #Find_Package(SDL2 REQUIRED)
    #BuildExternalCMake(sdl_build ${sdl_VERSION} ${sdl_SOURCE_DIR} "" "SDL2::SDL2;SDL2::SDL2main")
    # Save and strip -fno-exceptions/-fno-rtti for third-party subdirectories
    # that need exceptions (vio tests, fmt, etc). Restore after.
    set(_SAVED_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    add_subdirectory(${sdl_SOURCE_DIR})

    add_subdirectory(${vio_SOURCE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/vio_build")

    list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/FindPackage/laszip)
    CmDepInstallDir(LASZIP_INSTALL_DIR laszip_build ${laszip_VERSION})
    Find_Package(laszip REQUIRED)
    # Patch laszip 3.5.0 source files (only once after fresh extraction).
    # Guard with a stamp file to avoid re-patching (and corrupting) on
    # subsequent configure runs.
    set(_laszip_patch_stamp "${laszip_SOURCE_DIR}/.points_patched")
    if(NOT EXISTS "${_laszip_patch_stamp}")
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
        # laszip 3.5.0 doesn't define LASZIP_DYN_LINK or LASZIP_SOURCE when
        # building the API DLL, so LASZIP_API expands to nothing and no symbols
        # are exported.  Add the required compile definitions.
        file(APPEND "${laszip_SOURCE_DIR}/dll/CMakeLists.txt"
            "\ntarget_compile_definitions(\${LASZIP_API_LIB_NAME} PRIVATE LASZIP_DYN_LINK LASZIP_SOURCE)\n")
        file(WRITE "${_laszip_patch_stamp}" "")
    endif()
    set(LASZIP_CMAKE_OPTIONS "-DCMAKE_CXX_STANDARD=17;-DCMAKE_CXX_STANDARD_REQUIRED=ON")
    if (WIN32)
        list(APPEND LASZIP_CMAKE_OPTIONS "-DCMAKE_DEBUG_POSTFIX=d")
        # Export all symbols from laszip DLLs so that functions like
        # laszip_load_dll (which lack the LASZIP_API decorator) are visible.
        list(APPEND LASZIP_CMAKE_OPTIONS "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON")
    endif ()
    CmDepBuildExternal(laszip_build ${laszip_VERSION} ${laszip_SOURCE_DIR} "${LASZIP_CMAKE_OPTIONS}" "laszip::api;laszip::impl")

    set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(${fmt_SOURCE_DIR})
    # Clang on MinGW doesn't properly auto-detect -fno-exceptions for fmt headers
    if (NOT MSVC)
        target_compile_definitions(fmt PUBLIC FMT_EXCEPTIONS=0)
    endif ()
    add_subdirectory(${glm_SOURCE_DIR})
    add_subdirectory(${catch2_SOURCE_DIR})
    add_subdirectory(${unordered_dense_SOURCE_DIR})
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
    set(BLOSC_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory(${blosc2_SOURCE_DIR} SYSTEM)
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
