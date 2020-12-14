find_package(OpenGL REQUIRED)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DroidSans.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Karla-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Karla-Regular.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/ProggyClean.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/ProggyClean.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/ProggyTiny.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/ProggyTiny.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto-Medium.ttf COPYONLY)

add_executable(frustum frustum.cpp glad.c
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui.h
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/examples/imgui_impl_sdl.cpp
  ${imgui_SOURCE_DIR}/examples/imgui_impl_opengl3.cpp
  )
target_include_directories(frustum PRIVATE include ${imgui_SOURCE_DIR})
cmrc_add_resource_library(frustum_shaders ALIAS frustum::shaders NAMESPACE shaders shaders/simple.frag shaders/simple.vert)
cmrc_add_resource_library(frustum_fonts   ALIAS frustum::fonts NAMESPACE fonts 
 fonts/Cousine-Regular.ttf
 fonts/DroidSans.ttf
 fonts/Karla-Regular.ttf
 fonts/ProggyClean.ttf
 fonts/ProggyTiny.ttf
 fonts/Roboto-Medium.ttf
)
target_link_libraries(frustum PRIVATE points_render fmt glm SDL2 SDL2main OpenGL::GL frustum::shaders frustum::fonts)
target_compile_definitions(frustum PRIVATE FONTS_DIR=)
copyDllForTarget(frustum SDL2)
setWarningFlagsForTarget(frustum)
