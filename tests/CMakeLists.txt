add_library(catch_main STATIC catch-main.cpp)
target_link_libraries(catch_main Catch2::Catch2)

add_executable(public_interface_unit_tests "public/renderer_base.cpp")
target_link_libraries(public_interface_unit_tests PRIVATE catch_main Catch2::Catch2 fmt points_render)
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
	#target_precompile_headers(public_interface_unit_tests PRIVATE <catch2/catch.hpp>)
endif()

add_executable(private_interface_unit_tests
	private/basic_camera.cpp
	private/morton_tests.cpp
	private/tree_tests.cpp
    private/vector_updater_tests.cpp
    private/fixed_size_vector_tests.cpp
    private/converter_tests.cpp
	private/deque_map_test.cpp
	private/blob_manager_test.cpp
	$<TARGET_OBJECTS:common_objects>
	$<TARGET_OBJECTS:converter_objects>
	$<TARGET_OBJECTS:points_render_objects>
)
BuildExternalTargetLinkLibrary(private_interface_unit_tests PRIVATE catch_main Catch2::Catch2 fmt glm libuv::libuv laszip::api)
target_include_directories(private_interface_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/render ${libmorton_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src/common ${PROJECT_SOURCE_DIR}/src/render ${PROJECT_SOURCE_DIR}/src/converter ${libmorton_SOURCE_DIR}/include)
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
	#target_precompile_headers(private_interface_unit_tests PRIVATE <catch2/catch.hpp>)
endif()
copy_dll_for_target(public_interface_unit_tests points_render converter)
