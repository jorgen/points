# Enumerate the dependency set a cmake-dep packages file declares, without fetching anything.
#
#   cmake [-DDEW_BUILD_PYTHON=ON] \
#         -DCMDEP_PACKAGES_FILE=/abs/path/CMake/3rdPartyPackages.cmake \
#         -DCMDEP_ENUM_OUT=/abs/path/manifest.txt \
#         [-DCMDEP_PROJECT=dew] \
#         -P enumerate.cmake
#
# One record per declared dependency, in declaration order:
#
#   KIND|name|version|dirname|ALGO|hex|url|payload
#
#   KIND    PACKAGE (archive extracted to <dirname>/), FILE (single downloaded file),
#           or SYSTEM (USE_SYSTEM is on, so nothing lands in 3rdparty/)
#   dirname the exact top-level entry cmake-dep creates under 3rdparty/
#   payload "" for PACKAGE/SYSTEM, the destination filename for FILE
#
# The point of doing this in CMake rather than with a shell regex is that the packages file is
# evaluated by the real interpreter: `if (DEW_BUILD_PYTHON)` blocks, comments and continuations
# are handled by definition rather than by imitation. If it ever grows a cmake-dep command this
# shim does not model, CMake fails loudly here instead of silently under-reporting the set.
#
# Override resolution mirrors CmDepFetchPackage's (<PREFIX>_<DEP>_VERSION / _URL / _<ALGO> and
# <PREFIX>_USE_SYSTEM_<DEP>), so a build configured with -D overrides enumerates what that build
# actually fetches.

cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED CMDEP_PACKAGES_FILE)
    message(FATAL_ERROR "enumerate: -DCMDEP_PACKAGES_FILE=<path> is required")
endif ()
get_filename_component(CMDEP_PACKAGES_FILE "${CMDEP_PACKAGES_FILE}" ABSOLUTE)
if (NOT EXISTS "${CMDEP_PACKAGES_FILE}")
    message(FATAL_ERROR "enumerate: packages file not found: ${CMDEP_PACKAGES_FILE}")
endif ()

# cmake-dep derives the knob prefix from CMDEP_PROJECT, falling back to PROJECT_NAME — which does
# not exist in script mode, so it has to be passed in. Required rather than defaulted: this must be
# the consuming project's project() name (dewfall -> DEWFALL_<DEP>_VERSION and friends), and a
# plausible-looking wrong default silently resolves a different set of override knobs than the build
# does. The failure that produces is nasty and quiet — the manifest names a version the build did not
# fetch, so the fetched directory is pruned as "stale" on every run and never enters the cache.
if (NOT DEFINED CMDEP_PROJECT OR CMDEP_PROJECT STREQUAL "")
    message(FATAL_ERROR "enumerate: -DCMDEP_PROJECT=<project() name> is required "
                        "(cmake-dep derives its override-knob prefix from it)")
endif ()
string(TOUPPER "${CMDEP_PROJECT}" _prefix)
string(MAKE_C_IDENTIFIER "${_prefix}" _prefix)

set(_records "")

macro(_emit kind name version algo hex url payload)
    list(APPEND _records "${kind}|${name}|${version}|${name}-${version}|${algo}|${hex}|${url}|${payload}")
endmacro()

macro(_resolve name raw_version raw_url raw_hash out_ver out_url out_algo out_hex out_sys)
    string(TOUPPER "${name}" _d)
    string(MAKE_C_IDENTIFIER "${_d}" _d)

    string(REGEX MATCH "^([^=]+)=(.*)$" _m "${raw_hash}")
    if (NOT _m)
        message(FATAL_ERROR "enumerate: hash for '${name}' must be ALGO=HEX, got '${raw_hash}'")
    endif ()
    set(${out_algo} "${CMAKE_MATCH_1}")
    set(${out_hex} "${CMAKE_MATCH_2}")

    set(${out_ver} "${raw_version}")
    if (DEFINED ${_prefix}_${_d}_VERSION AND NOT "${${_prefix}_${_d}_VERSION}" STREQUAL "")
        set(${out_ver} "${${_prefix}_${_d}_VERSION}")
    endif ()
    set(${out_url} "${raw_url}")
    if (DEFINED ${_prefix}_${_d}_URL AND NOT "${${_prefix}_${_d}_URL}" STREQUAL "")
        set(${out_url} "${${_prefix}_${_d}_URL}")
    endif ()
    if (DEFINED ${_prefix}_${_d}_${${out_algo}} AND NOT "${${_prefix}_${_d}_${${out_algo}}}" STREQUAL "")
        set(${out_hex} "${${_prefix}_${_d}_${${out_algo}}}")
    endif ()

    set(${out_sys} OFF)
    if (${_prefix}_USE_SYSTEM_${_d})
        set(${out_sys} ON)
    endif ()
endmacro()

macro(CmDepFetchPackage name version url url_hash)
    _resolve("${name}" "${version}" "${url}" "${url_hash}" _v _u _a _h _s)
    if (_s)
        _emit(SYSTEM "${name}" "${_v}" "${_a}" "${_h}" "${_u}" "")
    else ()
        _emit(PACKAGE "${name}" "${_v}" "${_a}" "${_h}" "${_u}" "")
    endif ()
endmacro()

macro(CmDepFetchFile name version url destination_name url_hash)
    _resolve("${name}" "${version}" "${url}" "${url_hash}" _v _u _a _h _s)
    _emit(FILE "${name}" "${_v}" "${_a}" "${_h}" "${_u}" "${destination_name}")
endmacro()

include("${CMDEP_PACKAGES_FILE}")

list(LENGTH _records _count)
if (_count EQUAL 0)
    message(FATAL_ERROR "enumerate: parsed 0 dependencies from ${CMDEP_PACKAGES_FILE} -- refusing to report an empty set")
endif ()

set(_text "")
foreach (_r IN LISTS _records)
    string(APPEND _text "${_r}\n")
endforeach ()

if (DEFINED CMDEP_ENUM_OUT AND NOT CMDEP_ENUM_OUT STREQUAL "")
    file(WRITE "${CMDEP_ENUM_OUT}" "${_text}")
else ()
    # message(NOTICE) goes to stderr, so `$(cmake -P ...)` captures nothing without a redirect.
    foreach (_r IN LISTS _records)
        message(NOTICE "${_r}")
    endforeach ()
endif ()
