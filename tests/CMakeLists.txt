add_library(doctest_main STATIC doctest-main.cpp)
target_link_libraries(doctest_main PUBLIC doctest)
target_compile_definitions(doctest_main PUBLIC DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

add_executable(public_interface_unit_tests "public/renderer_base.cpp")
target_link_libraries(public_interface_unit_tests PRIVATE doctest_main doctest fmt points_render)

add_executable(private_interface_unit_tests
        private/basic_camera.cpp
        private/morton_tests.cpp
        private/tree_tests.cpp
        private/vector_updater_tests.cpp
        private/fixed_size_vector_tests.cpp
        private/converter_tests.cpp
        private/deque_map_test.cpp
        private/blob_manager_test.cpp
        $<TARGET_OBJECTS:points_common_objects>
        $<TARGET_OBJECTS:points_converter_objects>
        $<TARGET_OBJECTS:points_render_objects>
        private/frustum.cpp
        private/byte_shuffle_tests.cpp
        private/compression_preprocess_tests.cpp
        private/compressor_tests.cpp
        private/camera_arcball_tests.cpp
        private/frame_node_registry_tests.cpp
        private/node_selector_tests.cpp
        private/draw_emitter_tests.cpp
        private/memory_writer_tests.cpp
)
target_link_libraries(private_interface_unit_tests PRIVATE vio libzstd_static)
target_link_libraries(private_interface_unit_tests PRIVATE doctest_main doctest fmt glm unordered_dense laszip)
target_include_directories(private_interface_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/render ${libmorton_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/common ${PROJECT_SOURCE_DIR}/src/render ${PROJECT_SOURCE_DIR}/src/converter ${libmorton_SOURCE_DIR}/include)
copy_dll_for_target(public_interface_unit_tests points_render points_converter)
copy_dll_for_target(private_interface_unit_tests points_render points_converter)
