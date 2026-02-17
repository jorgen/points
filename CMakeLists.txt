cmake_minimum_required(VERSION 3.30)
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif ()

project(Points VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(FetchContent)
FetchContent_Declare(cmake-dep
    GIT_REPOSITORY https://github.com/jorgen/cmake-dep.git
    GIT_TAG 2667e3f
)
FetchContent_MakeAvailable(cmake-dep)
list(APPEND CMAKE_MODULE_PATH "${cmake-dep_SOURCE_DIR}/cmake")
include(CmDepMain)

include(GNUInstallDirs)
include(GenerateExportHeader)

get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
if ("${LIB64}" STREQUAL "TRUE" AND NOT FORCE_NO_LIBDIR_SUFFIX)
    set(LIBSUFFIX 64)
else ()
    set(LIBSUFFIX "")
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (WIN32)
    add_compile_definitions(NOMINMAX=1 WIN32_LEAN_AND_MEAN=1 VC_EXTRALEAN=1)
endif ()
if (MSVC)
    string(REGEX REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_HAS_EXCEPTIONS=0")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")
endif ()

if (WIN32)
    if (CMAKE_VS_PLATFORM_TOOLSET)
        string(REGEX REPLACE "^v" "" MSVC_TOOLSET_VERSION_LOCAL "${CMAKE_VS_PLATFORM_TOOLSET}")
    else ()
        set(MSVC_TOOLSET_VERSION_LOCAL ${MSVC_TOOLSET_VERSION})
    endif ()
endif ()

option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_SHARED_LIBS "Build shared libs" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
find_package(Threads)

include(CMake/SetWarnings.cmake)

set(CMDEP_3RD_PARTY_PACKAGES_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CMake/3rdPartyPackages.cmake")
CmDepFetch3rdParty()

include(CMake/Build3rdParty.cmake)
Build3rdParty()
include(CMake/AddSystemHeadersToTarget.cmake)
include(CMake/CopyDllForTarget.cmake)

add_subdirectory(src)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (NOT DEFINED CPACK_GENERATOR)
    if (UNIX)
        set(CPACK_GENERATOR "TGZ")
    elseif (WIN32)
        set(CPACK_GENERATOR "ZIP")
    endif ()
endif ()

if (UNIX)
    find_program(LSB_RELEASE_EXEC lsb_release)
    if (LSB_RELEASE_EXEC)
        execute_process(COMMAND ${LSB_RELEASE_EXEC} -irs
                OUTPUT_VARIABLE OS_NAME
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else ()
        set(OS_NAME "linux")
    endif ()
else ()
    # Always set OS_NAME to "win" for windows
    set(OS_NAME "win-${MSVC_TOOLSET_VERSION_LOCAL}")
endif ()

if (NOT DEFINED PACKAGE_RELEASE)
    set(PACKAGE_RELEASE "1")
endif ()

set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package-install")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${PACKAGE_RELEASE}-${OS_NAME}")

include(CPack)

