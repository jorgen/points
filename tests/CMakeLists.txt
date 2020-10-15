add_library(catch_main STATIC catch-main.cpp)
target_link_libraries(catch_main Catch2::Catch2)

add_executable(unit_tests renderer_base.cpp)
target_link_libraries(unit_tests PRIVATE catch_main Catch2::Catch2 fmt points_render)
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
	target_precompile_headers(unit_tests PRIVATE <catch2/catch.hpp>)
endif()
