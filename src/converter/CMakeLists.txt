
set(public_headers
        dew/converter/export.h
        dew/converter/converter.h
        dew/converter/connection_cli.h
        dew/converter/default_attribute_names.h
        dew/converter/converter_data_source.h
)

set(private_headers
        processor.hpp
        reader.hpp
        conversion_types.hpp
        processor.hpp
        converter.hpp
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
        data_source_node_bbox.hpp
        frustum_tree_walker.hpp
        render_node.hpp
        render_pipeline.hpp
        input_data_source_registry.hpp
        input_storage_map.hpp
        native_node_data_loader.hpp
        compressor.hpp
        perf_stats.hpp
        byte_shuffle.hpp
        compressor_zstd.hpp
        compressor_fse.hpp
        compressor_ans.hpp
        compression_preprocess.hpp
        url.hpp
        index_format.hpp
        storage_backend.hpp
        packed_file_backend.hpp
        object_backend.hpp
)

set(sources
        converter.cpp
        processor.cpp
        reader.cpp
        input_header.cpp
        laszip_file_convert_callbacks.cpp
        sorter.cpp
        tree.cpp
        pre_init_file_retriever.cpp
        storage_handler.cpp
        tree_handler.cpp
        tree_collapse.cpp
        tree_lod_generator.cpp
        attributes_configs.cpp
        data_source_converter.cpp
        data_source_node_bbox.cpp
        frustum_tree_walker.cpp
        render_pipeline.cpp
        virtual_tree.cpp
        native_node_data_loader.cpp
        node_decode.cpp
        blob_manager.cpp
        blob_residency.cpp
        bucket_format.cpp
        upload_handler.cpp
        spill_store.cpp
        file_hole_punch.cpp
        input_data_source_registry.cpp
        input_storage_map.cpp
        compressor.cpp
        byte_shuffle.cpp
        compressor_zstd.cpp
        compressor_fse.cpp
        compressor_ans.cpp
        compression_preprocess.cpp
        storage_backend.cpp
        packed_file_backend.cpp
        object_backend.cpp
)

add_library(dew_converter_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(dew_converter_objects PRIVATE fmt glm unordered_dense dew_render dew_core vio_objstore libzstd_static)
target_link_libraries(dew_converter_objects PRIVATE laszip)
target_include_directories(dew_converter_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(dew_converter_objects
        PRIVATE
        ${libmorton_SOURCE_DIR}/include
        ${zstd_SOURCE_DIR}/lib
        ${zstd_SOURCE_DIR}/lib/common
)
setWarningflagsForTarget(dew_converter_objects)

if (MSVC)
    target_compile_options(dew_converter_objects PRIVATE /arch:AVX2)
else ()
    #target_compile_options(dew_converter_objects PRIVATE -mavx2)
endif ()

if (BUILD_SHARED_LIBS)
    target_compile_definitions(dew_converter_objects PRIVATE DEW_CONVERTER_EXPORTS)
    #    target_compile_options(dew_converter_objects PRIVATE -fsanitize=address)
    #    target_link_libraries(dew_converter_objects PRIVATE -fsanitize=address)
endif ()

add_library(dew_converter empty_dylib_function.cpp $<TARGET_OBJECTS:dew_converter_objects>)
#target_compile_options(dew_converter PRIVATE -fsanitize=address)
#target_link_options(dew_converter PRIVATE -fsanitize=address)
target_link_libraries(dew_converter PUBLIC dew_converter_objects)
target_link_libraries(dew_converter PRIVATE laszip)
