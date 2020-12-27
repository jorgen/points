#This file depends on being in points/CMake directory
set(Fetch3rdPartyDirInternal "${CMAKE_CURRENT_LIST_DIR}")
macro(Fetch3rdParty_Package name version url url_hash)
  if (POINTS_3RD_PARTY_DIR)
    set(Fetch3rdPartyDir "${POINTS_3RD_PARTY_DIR}")
  else()
    set(Fetch3rdPartyDir "${Fetch3rdPartyDirInternal}/../3rdparty")
  endif()
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
  endif()
endmacro()

function(Fetch3rdParty)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  Fetch3rdParty_Package(fmt    7.1.3      https://github.com/fmtlib/fmt/archive/7.1.3.tar.gz        SHA256=5cae7072042b3043e12d53d50ef404bbb76949dad1de368d7f993a15c8c05ecc)
  Fetch3rdParty_Package(sdl    2.0.14     https://www.libsdl.org/release/SDL2-2.0.14.tar.gz         SHA256=d8215b571a581be1332d2106f8036fcb03d12a70bae01e20f424976d275432bc)
  Fetch3rdParty_Package(glm    0.9.9.8    https://github.com/g-truc/glm/archive/0.9.9.8.tar.gz      SHA256=7d508ab72cb5d43227a3711420f06ff99b0a0cb63ee2f93631b162bfe1fe9592)
  Fetch3rdParty_Package(catch2 0.9.9.8    https://github.com/catchorg/Catch2/archive/v2.13.2.tar.gz SHA256=5e39d9199f4f174dc3c8896fb4cf0a2ce9b9c358ae759b87fade6d615ca2d27e)
  Fetch3rdParty_Package(cmakerc e7ba9e    https://github.com/vector-of-bool/cmrc/archive/e7ba9e9417960b2a5cefc9e79e8af8b06bfde3d1.zip SHA256=75c1263bb37b8bae159bacb4da10fd2eb50b9c04118901218b6a817b9d0fa757)
  Fetch3rdParty_Package(imgui  1.79.0     https://github.com/ocornut/imgui/archive/v1.79.tar.gz     SHA256=f1908501f6dc6db8a4d572c29259847f6f882684b10488d3a8d2da31744cd0a4)
  Fetch3rdParty_Package(libjpeg 9.d       https://ijg.org/files/jpegsrc.v9d.tar.gz                  SHA256=99cb50e48a4556bc571dadd27931955ff458aae32f68c4d9c39d624693f69c32)

endfunction()

