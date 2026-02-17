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
    Fetch3rdParty_Package(sdl 3.1.6 https://github.com/libsdl-org/SDL/archive/refs/tags/preview-3.1.6.tar.gz SHA256=5da5e265c150b954d007bf1465b155d9df1d0d52f10115a49bb918dc8fe2826a)
    Fetch3rdParty_Package(glm 1.0.1 https://github.com/g-truc/glm/archive/1.0.1.tar.gz SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
    Fetch3rdParty_Package(catch2 0.9.9.8 https://github.com/catchorg/Catch2/archive/v2.13.2.tar.gz SHA256=5e39d9199f4f174dc3c8896fb4cf0a2ce9b9c358ae759b87fade6d615ca2d27e)
    Fetch3rdParty_Package(cmakerc 952ff https://github.com/vector-of-bool/cmrc/archive/952ffddba731fc110bd50409e8d2b8a06abbd237.zip SHA256=b199e7481dda667cd1b1936c9acb64e496ebc3c5ad90b381ba8d0f361c80638d)
    Fetch3rdParty_Package(imgui 1.91.6 https://github.com/ocornut/imgui/archive/v1.91.6.tar.gz SHA256=c5fbc5dcab1d46064001c3b84d7a88812985cde7e0e9ced03f5677bec1ba502a)
    Fetch3rdParty_Package(vio 3509474 https://github.com/jorgen/vio/archive/350947496500c33df7b45c32a3a3a32ee670690e.tar.gz SHA256=6191374d5139cc2da53a72c763160a3fe5702b5f7e67c8d08299fba7d44e287d)
    Fetch3rdParty_Package(curl 7.74.0 https://github.com/curl/curl/releases/download/curl-7_74_0/curl-7.74.0.tar.gz SHA256=e56b3921eeb7a2951959c02db0912b5fcd5fdba5aca071da819e1accf338bbd7)
    Fetch3rdParty_Package(laszip 3.5.0 https://github.com/LASzip/LASzip/archive/refs/tags/3.5.0.tar.gz SHA256=6e9baac8689dfd2e1502ceafabb20c62b6cd572744d240fb755503fd57c2a6af)
    Fetch3rdParty_Package(libmorton 0.2.10 https://github.com/Forceflow/libmorton/archive/v0.2.10.tar.gz SHA256=5f4a8dbf350222b0bec35ad1bee6b76fa22e16ea364e1e80ca47d8f5a7210d6f)
    Fetch3rdParty_Package(argh 431bf323ac https://github.com/adishavit/argh/archive/431bf323acd3bb805483491ff277dac5d85772ad.tar.gz SHA256=af6579d9143b9e2445354796645648c2bbbecdbe3705ba825221861a95f936ea)
    Fetch3rdParty_Package(unordered_dense 4.1.2 https://github.com/martinus/unordered_dense/archive/refs/tags/v4.1.2.tar.gz SHA256=300410dbcd32800f83b2113dfecbdfe8cd256caa4cfeb117d646021d6e3209ae)
    Fetch3rdParty_File(stbimage b42009b https://raw.githubusercontent.com/nothings/stb/8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55/stb_image.h stb_image.h SHA256=91f435e0fc6a620018b878b9859c74dff60d28046f87e649191ad6f35a98c722)
endfunction()

