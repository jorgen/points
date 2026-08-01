set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(dew_info
        dew_info.cpp
)

CmDepTargetLinkLibrary(dew_info PRIVATE dew_converter dew_common fmt)
copy_dll_for_target(dew_info dew_converter dew_common dew_render)
setWarningFlagsForTarget(dew_info)
set_example_properties(dew_info)
