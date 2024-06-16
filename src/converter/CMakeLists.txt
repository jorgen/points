include(BuildExternalTargetLinkLibrary)

Find_Package(libuv REQUIRED)
Find_Package(CURL REQUIRED)

set(public_headers
        points/converter/export.h
        points/converter/converter.h
        points/converter/default_attribute_names.h
        points/converter/converter_data_source.h
)

set(private_headers
        processor.hpp
        reader.hpp
        threaded_event_loop.hpp
        conversion_types.hpp
        event_pipe.hpp
        processor.hpp
        converter.hpp
        worker.hpp
        input_header.hpp
        morton.hpp
        sorter.hpp
        memcpy_array.hpp
        tree.hpp
        point_buffer_splitter.hpp
        pre_init_file_retriever.hpp
        storage_handler.hpp
        tree_handler.hpp
        tree_lod_generator.hpp
        attributes_configs.hpp
        data_source_converter.hpp
        frustum_tree_walker.hpp
        input_data_source_registry.hpp
        input_storage_map.hpp
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
        pre_init_file_retriever.cpp
        storage_handler.cpp
        tree_handler.cpp
        tree_lod_generator.cpp
        attributes_configs.cpp
        data_source_converter.cpp
        frustum_tree_walker.cpp
        blob_manager.cpp
        input_data_source_registry.cpp
        input_storage_map.cpp
)

add_library(converter_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(converter_objects PRIVATE fmt glm unordered_dense points_render common)
BuildExternalTargetLinkLibrary(converter_objects PRIVATE libuv::libuv CURL::libcurl laszip::api)
target_include_directories(converter_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(converter_objects
        PRIVATE
        ${libmorton_SOURCE_DIR}/include
)
setWarningflagsForTarget(converter_objects)

if (MSVC)
    target_compile_options(converter_objects PRIVATE /arch:AVX2)
else ()
    #target_compile_options(converter_objects PRIVATE -mavx2)
endif ()

if (BUILD_SHARED_LIBS)
    target_compile_definitions(converter_objects PRIVATE POINTS_CONVERTER_EXPORTS)
    #    target_compile_options(converter_objects PRIVATE -fsanitize=address)
    #    target_link_libraries(converter_objects PRIVATE -fsanitize=address)
endif ()

add_library(converter empty_dylib_function.cpp $<TARGET_OBJECTS:converter_objects>)
#target_compile_options(converter PRIVATE -fsanitize=address)
#target_link_options(converter PRIVATE -fsanitize=address)
BuildExternalTargetLinkLibrary(converter PUBLIC converter_objects)
