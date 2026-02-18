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
    # Patch laszip 3.5.0: it uses add_compile_options(-std=c++17) which wrongly
    # applies C++ flags to C files. Remove those lines since CMAKE_CXX_STANDARD 17
    # is already set on line 4 of laszip's CMakeLists.txt.
    file(READ "${laszip_SOURCE_DIR}/CMakeLists.txt" _laszip_cml)
    string(REPLACE "add_compile_options(\"/std:c++17\")" "# patched out: add_compile_options(\"/std:c++17\")" _laszip_cml "${_laszip_cml}")
    string(REPLACE "add_compile_options(-std=c++17)" "# patched out: add_compile_options(-std=c++17)" _laszip_cml "${_laszip_cml}")
    file(WRITE "${laszip_SOURCE_DIR}/CMakeLists.txt" "${_laszip_cml}")
    # Also patch macros.cmake: LASZIP_ADD_LIBRARY sets CXX_STANDARD 11 on targets,
    # overriding the global CMAKE_CXX_STANDARD 17. Fix to use 17.
    file(READ "${laszip_SOURCE_DIR}/cmake/macros.cmake" _laszip_macros)
    string(REPLACE "CXX_STANDARD 11" "CXX_STANDARD 17" _laszip_macros "${_laszip_macros}")
    file(WRITE "${laszip_SOURCE_DIR}/cmake/macros.cmake" "${_laszip_macros}")
    # Patch dll/CMakeLists.txt: laszip 3.5.0 doesn't define LASZIP_DYN_LINK or
    # LASZIP_SOURCE when building the API DLL, so LASZIP_API expands to nothing
    # and no symbols are exported. Add the required compile definitions.
    file(APPEND "${laszip_SOURCE_DIR}/dll/CMakeLists.txt"
        "\ntarget_compile_definitions(\${LASZIP_API_LIB_NAME} PRIVATE LASZIP_DYN_LINK LASZIP_SOURCE)\n")
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
    set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})
    include(${cmakerc_SOURCE_DIR}/CMakeRC.cmake)

    # Restore original flags for points' own code
    set(CMAKE_CXX_FLAGS ${_SAVED_CMAKE_CXX_FLAGS})
endmacro()
