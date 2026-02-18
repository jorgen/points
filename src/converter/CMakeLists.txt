
set(public_headers
        points/converter/export.h
        points/converter/converter.h
        points/converter/default_attribute_names.h
        points/converter/converter_data_source.h
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
        frustum_tree_walker.hpp
        input_data_source_registry.hpp
        input_storage_map.hpp
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
        tree_lod_generator.cpp
        attributes_configs.cpp
        data_source_converter.cpp
        frustum_tree_walker.cpp
        blob_manager.cpp
        input_data_source_registry.cpp
        input_storage_map.cpp
)

add_library(points_converter_objects OBJECT ${public_headers} ${private_headers} ${sources})
target_link_libraries(points_converter_objects PRIVATE fmt glm unordered_dense points_render points_common vio)
CmDepBuildExternalTargetLinkLibrary(points_converter_objects PRIVATE laszip::impl)
target_include_directories(points_converter_objects
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_include_directories(points_converter_objects
        PRIVATE
        ${libmorton_SOURCE_DIR}/include
)
setWarningflagsForTarget(points_converter_objects)

if (MSVC)
    target_compile_options(points_converter_objects PRIVATE /arch:AVX2)
else ()
    #target_compile_options(points_converter_objects PRIVATE -mavx2)
endif ()

if (BUILD_SHARED_LIBS)
    target_compile_definitions(points_converter_objects PRIVATE POINTS_CONVERTER_EXPORTS)
    #    target_compile_options(points_converter_objects PRIVATE -fsanitize=address)
    #    target_link_libraries(points_converter_objects PRIVATE -fsanitize=address)
endif ()

add_library(points_converter empty_dylib_function.cpp $<TARGET_OBJECTS:points_converter_objects>)
#target_compile_options(points_converter PRIVATE -fsanitize=address)
#target_link_options(points_converter PRIVATE -fsanitize=address)
CmDepBuildExternalTargetLinkLibrary(points_converter PUBLIC points_converter_objects)
