set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(dew_info
        dew_info.cpp
        $<TARGET_OBJECTS:dew_common_objects>
        $<TARGET_OBJECTS:dew_converter_objects>
        $<TARGET_OBJECTS:dew_render_objects>
)

target_link_libraries(dew_info PRIVATE vio_objstore libzstd_static)
target_link_libraries(dew_info PRIVATE fmt glm unordered_dense laszip)
target_include_directories(dew_info PRIVATE
        ${PROJECT_SOURCE_DIR}/src/converter
        ${PROJECT_SOURCE_DIR}/src/common
        ${PROJECT_SOURCE_DIR}/src/render
        ${libmorton_SOURCE_DIR}/include
)
copy_dll_for_target(dew_info dew_converter dew_common dew_render)
set_tool_properties(dew_info)
