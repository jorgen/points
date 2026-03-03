set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(jlp_extract
        jlp_extract.cpp
        $<TARGET_OBJECTS:points_common_objects>
        $<TARGET_OBJECTS:points_converter_objects>
        $<TARGET_OBJECTS:points_render_objects>
)

target_link_libraries(jlp_extract PRIVATE vio blosc2_static)
target_link_libraries(jlp_extract PRIVATE fmt glm unordered_dense laszip)
target_include_directories(jlp_extract PRIVATE
        ${PROJECT_SOURCE_DIR}/src/converter
        ${PROJECT_SOURCE_DIR}/src/common
        ${PROJECT_SOURCE_DIR}/src/render
        ${libmorton_SOURCE_DIR}/include
)
copy_dll_for_target(jlp_extract points_converter points_common points_render)
set_tool_properties(jlp_extract)
