
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(converter_example
        converter_example.cpp
)

CmDepTargetLinkLibrary(converter_example PRIVATE points_converter points_common points_render fmt)
copy_dll_for_target(converter_example points_converter points_common points_render)
setWarningFlagsForTarget(converter_example)
set_example_properties(converter_example)