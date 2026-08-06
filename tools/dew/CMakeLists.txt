set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(dew
        main.cpp
        tool_common.cpp
        cmd_convert.cpp
        cmd_copy.cpp
        cmd_extract.cpp
        cmd_info.cpp
        cmd_laz.cpp
        cmd_query.cpp
        $<TARGET_OBJECTS:dew_core_objects>
        $<TARGET_OBJECTS:dew_access_objects>
        $<TARGET_OBJECTS:dew_converter_objects>
        $<TARGET_OBJECTS:dew_render_objects>
)

target_link_libraries(dew PRIVATE vio_objstore libzstd_static)
target_link_libraries(dew PRIVATE fmt glm unordered_dense laszip)
target_include_directories(dew PRIVATE
        ${PROJECT_SOURCE_DIR}/src/converter
        ${PROJECT_SOURCE_DIR}/src/access
        ${PROJECT_SOURCE_DIR}/src/core
        ${PROJECT_SOURCE_DIR}/src/render
        ${libmorton_SOURCE_DIR}/include
)
target_include_directories(dew SYSTEM PRIVATE ${argh_SOURCE_DIR})
target_compile_definitions(dew PRIVATE DEW_CLI_VERSION="${PROJECT_VERSION}")
if(MSVC)
  # Expand shell wildcards in positional args (dew convert *.laz).
  target_link_options(dew PRIVATE "setargv.obj")
endif()
copy_dll_for_target(dew dew_converter dew_core dew_render)
setWarningFlagsForTarget(dew)
set_tool_properties(dew)
