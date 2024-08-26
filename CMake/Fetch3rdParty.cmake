#This file depends on being in points/CMake directory
set(Fetch3rdPartyDirInternal "${CMAKE_CURRENT_LIST_DIR}")
macro(Fetch3rdParty_Package name version url url_hash)
    if (POINTS_3RD_PARTY_DIR)
        set(Fetch3rdPartyDir "${POINTS_3RD_PARTY_DIR}")
    else ()
        set(Fetch3rdPartyDir "${Fetch3rdPartyDirInternal}/../3rdparty")
    endif ()
    get_filename_component(thirdParty "${Fetch3rdPartyDir}" ABSOLUTE)
    set(SRC_DIR ${thirdParty}/${name}-${version})
    set(${name}_SOURCE_DIR ${SRC_DIR} PARENT_SCOPE)
    set(${name}_VERSION ${version} PARENT_SCOPE)
    if (NOT (EXISTS ${SRC_DIR}))
        FetchContent_Populate(${name}
                URL ${url}
                URL_HASH ${url_hash}
                SOURCE_DIR ${SRC_DIR}
                SUBBUILD_DIR ${thirdParty}/CMakeArtifacts/${name}-sub-${version}
                BINARY_DIR ${thirdParty}/CMakeArtifacts/${name}-${version})
    endif ()
endmacro()

macro(Fetch3rdParty_File name version url destination_name url_hash)
    if (POINTS_3RD_PARTY_DIR)
        set(Fetch3rdPartyDir "${POINTS_3RD_PARTY_DIR}")
    else ()
        set(Fetch3rdPartyDir "${Fetch3rdPartyDirInternal}/../3rdparty")
    endif ()
    get_filename_component(thirdParty "${Fetch3rdPartyDir}" ABSOLUTE)
    file(MAKE_DIRECTORY ${thirdParty})
    set(SRC_DIR ${thirdParty}/${name}-${version})
    set(${name}_SOURCE_DIR ${SRC_DIR} PARENT_SCOPE)
    set(${name}_VERSION ${version} PARENT_SCOPE)
    set(DESTINATION_FILE "${SRC_DIR}/${destination_name}")
    if (NOT (EXISTS ${DESTINATION_FILE}))
        file(DOWNLOAD ${url} ${DESTINATION_FILE}
                SHOW_PROGRESS
                EXPECTED_HASH ${url_hash}
        )
    endif ()
endmacro()

function(Fetch3rdParty)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    Fetch3rdParty_Package(fmt 10.1.1 https://github.com/fmtlib/fmt/archive/10.1.1.tar.gz SHA256=78b8c0a72b1c35e4443a7e308df52498252d1cefc2b08c9a97bc9ee6cfe61f8b)
    Fetch3rdParty_Package(sdl 2.30.6 https://github.com/libsdl-org/SDL/releases/download/release-2.30.6/SDL2-2.30.6.tar.gz SHA256=c6ef64ca18a19d13df6eb22df9aff19fb0db65610a74cc81dae33a82235cacd4)
    Fetch3rdParty_Package(glm 1.0.1 https://github.com/g-truc/glm/archive/1.0.1.tar.gz SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
    Fetch3rdParty_Package(catch2 0.9.9.8 https://github.com/catchorg/Catch2/archive/v2.13.2.tar.gz SHA256=5e39d9199f4f174dc3c8896fb4cf0a2ce9b9c358ae759b87fade6d615ca2d27e)
    Fetch3rdParty_Package(cmakerc 2584030073 https://github.com/vector-of-bool/cmrc/archive/25840300733ef9429d895ae3b0c9322f05cd08aa.zip SHA256=f394a02fc11d52fef285406e0334f84089381e65b5a873c4a71bc6caf6bfee36)
    Fetch3rdParty_Package(imgui 1.79.0 https://github.com/ocornut/imgui/archive/v1.79.tar.gz SHA256=f1908501f6dc6db8a4d572c29259847f6f882684b10488d3a8d2da31744cd0a4)
    Fetch3rdParty_Package(libuv 1.40.0 https://github.com/libuv/libuv/archive/v1.40.0.tar.gz SHA256=70fe1c9ba4f2c509e8166c0ca2351000237da573bb6c82092339207a9715ba6b)
    Fetch3rdParty_Package(curl 7.74.0 https://github.com/curl/curl/releases/download/curl-7_74_0/curl-7.74.0.tar.gz SHA256=e56b3921eeb7a2951959c02db0912b5fcd5fdba5aca071da819e1accf338bbd7)
    Fetch3rdParty_Package(laszip 3.4.3 https://github.com/LASzip/LASzip/releases/download/3.4.3/laszip-src-3.4.3.tar.gz SHA256=53f546a7f06fc969b38d1d71cceb1862b4fc2c4a0965191a0eee81a57c7b373d)
    Fetch3rdParty_Package(libmorton 0.2.10 https://github.com/Forceflow/libmorton/archive/v0.2.10.tar.gz SHA256=5f4a8dbf350222b0bec35ad1bee6b76fa22e16ea364e1e80ca47d8f5a7210d6f)
    Fetch3rdParty_Package(argh 431bf323ac https://github.com/adishavit/argh/archive/431bf323acd3bb805483491ff277dac5d85772ad.tar.gz SHA256=af6579d9143b9e2445354796645648c2bbbecdbe3705ba825221861a95f936ea)
    Fetch3rdParty_Package(unordered_dense 4.1.2 https://github.com/martinus/unordered_dense/archive/refs/tags/v4.1.2.tar.gz SHA256=300410dbcd32800f83b2113dfecbdfe8cd256caa4cfeb117d646021d6e3209ae)
    Fetch3rdParty_File(stbimage b42009b https://raw.githubusercontent.com/nothings/stb/8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55/stb_image.h stb_image.h SHA256=91f435e0fc6a620018b878b9859c74dff60d28046f87e649191ad6f35a98c722)
endfunction()

