
set(CMAKE_INCLUDE_CURRENT_DIR ON)
add_executable(converter_example
        converter_example.cpp
)

CmDepTargetLinkLibrary(converter_example PRIVATE dew_converter dew_common dew_render fmt)
if(MSVC)
  target_link_options(converter_example PRIVATE "setargv.obj")
endif()
copy_dll_for_target(converter_example dew_converter dew_common dew_render)
setWarningFlagsForTarget(converter_example)
set_example_properties(converter_example)