include(BuildExternalTargetLinkLibrary)

Find_Package(libuv REQUIRED)
Find_Package(CURL REQUIRED)

set(public_headers
  points/converter/converter.h
  points/converter/export.h
  points/converter/error.h
  points/converter/default_attribute_names.h
  )

set(private_headers
  processor_p.h
  reader_p.h
  threaded_event_loop_p.h
  conversion_types_p.h
  event_pipe_p.h
  processor_p.h
  converter_p.h
  worker_p.h
  input_header_p.h
  morton_p.h
  sorter_p.h
  memcpy_array_p.h
  tree_p.h
  point_buffer_splitter_p.h
   )

set(sources
  converter.cpp 
  processor.cpp
  reader.cpp
  worker.cpp
  input_header.cpp
  laszip_file_convert_callbacks.cpp
  sorter.cpp
  tree.cpp
  )

add_library(converter_objects OBJECT ${public_headers} ${private_headers} ${sources}  )
target_link_libraries(converter_objects PRIVATE fmt glm)
BuildExternalTargetLinkLibrary(converter_objects PRIVATE libuv::libuv CURL::libcurl laszip::api)
target_include_directories(converter_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(converter_objects
  PRIVATE
   ${libmorton_SOURCE_DIR}
)
setWarningflagsForTarget(converter_objects)

if (MSVC)
  target_compile_options(converter_objects PRIVATE /arch:AVX2)
else()
  target_compile_options(converter_objects PRIVATE -mavx2)
endif()

if (BUILD_SHARED_LIBS)
    target_compile_definitions(converter_objects PRIVATE points_converter_EXPORTS)
endif()

add_library(converter empty_dylib_function.cpp $<TARGET_OBJECTS:converter_objects>)
BuildExternalTargetLinkLibrary(converter PUBLIC converter_objects)
