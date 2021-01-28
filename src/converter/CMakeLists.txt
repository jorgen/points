include(BuildExternalTargetLinkLibrary)

Find_Package(libuv REQUIRED)
Find_Package(CURL REQUIRED)

set(public_headers
  points/converter/converter.h)

set(private_headers
  "" "processor_p.h")

set(sources
  converter.cpp 
   "processor.cpp")
add_library(converter_objects OBJECT ${public_headers} ${private_headers} ${sources}  "processor_p.h")
target_link_libraries(converter_objects PRIVATE fmt glm)
BuildExternalTargetLinkLibrary(converter_objects PRIVATE libuv::libuv CURL::libcurl laszip::api)
target_include_directories(converter_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
setWarningflagsForTarget(converter_objects)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(converter_objects PRIVATE points_converter_EXPORTS)
endif()

add_library(converter empty_dylib_function.cpp $<TARGET_OBJECTS:converter_objects> "processor_p.h")
BuildExternalTargetLinkLibrary(converter PUBLIC converter_objects)
