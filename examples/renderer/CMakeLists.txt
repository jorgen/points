include(BuildExternalTargetLinkLibrary)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/DroidSans.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DroidSans.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Karla-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Karla-Regular.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/ProggyClean.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/ProggyClean.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/ProggyTiny.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/ProggyTiny.ttf COPYONLY)
configure_file(${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto-Medium.ttf COPYONLY)

add_executable(renderer_example
        renderer_example.cpp
        gl_renderer.cpp
        gl_renderer.h
        glad.c
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui.h
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/examples/imgui_impl_sdl.cpp
        ${imgui_SOURCE_DIR}/examples/imgui_impl_opengl3.cpp
)

target_include_directories(renderer_example PRIVATE include ${imgui_SOURCE_DIR})
cmrc_add_resource_library(renderer_example_shaders ALIAS renderer_example::shaders NAMESPACE shaders
        shaders/aabb.frag
        shaders/aabb.vert
        shaders/skybox.frag
        shaders/skybox.vert
        shaders/points.frag
        shaders/points.vert
        shaders/dynpoints.frag
        shaders/dynpoints.vert
)
cmrc_add_resource_library(renderer_example_fonts ALIAS renderer_example::fonts NAMESPACE fonts
        fonts/Cousine-Regular.ttf
        fonts/DroidSans.ttf
        fonts/Karla-Regular.ttf
        fonts/ProggyClean.ttf
        fonts/ProggyTiny.ttf
        fonts/Roboto-Medium.ttf
)

cmrc_add_resource_library(renderer_example_textures ALIAS renderer_example::textures NAMESPACE textures
        textures/back.jpg
        textures/bottom.jpg
        textures/front.jpg
        textures/left.jpg
        textures/right.jpg
        textures/top.jpg
)

BuildExternalTargetLinkLibrary(renderer_example PRIVATE points_render points_converter fmt glm OpenGL::GL renderer_example::shaders renderer_example::fonts renderer_example::textures SDL2::SDL2 SDL2::SDL2main)
copy_dll_for_target(renderer_example points_render points_converter SDL2::SDL2)
setWarningFlagsForTarget(renderer_example)
