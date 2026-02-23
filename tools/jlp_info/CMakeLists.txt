set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(jlp_info
        jlp_info.cpp
)

CmDepTargetLinkLibrary(jlp_info PRIVATE points_converter points_common fmt)
copy_dll_for_target(jlp_info points_converter points_common points_render)
setWarningFlagsForTarget(jlp_info)
set_tool_properties(jlp_info)
