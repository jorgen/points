include(BuildExternalTargetLinkLibrary)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(converter_example
        converter_example.cpp
)

BuildExternalTargetLinkLibrary(converter_example PRIVATE points_converter points_common fmt)
copy_dll_for_target(converter_example points_converter points_common)
setWarningFlagsForTarget(converter_example)
