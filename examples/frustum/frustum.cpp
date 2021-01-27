#include <fmt/printf.h>

#include <SDL.h>
#include "include/glad/glad.h"
#include <fmt/printf.h>
#include <stdio.h>

#include "gl_renderer.h"

#include <examples/imgui_impl_sdl.h>
#include <examples/imgui_impl_opengl3.h>

#include <points/render/renderer.h>
#include <points/render/camera.h>
#include <points/render/aabb.h>
#include <points/render/aabb_data_source.h>
#include <points/render/skybox_data_source.h>
#include <points/render/flat_points_data_source.h>

#include <vector>

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(fonts);
CMRC_DECLARE(textures);

template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> create_unique_ptr(T *t, Deleter d)
{
  return std::unique_ptr<T, decltype(d)>(t, d);
}

template<size_t N>
static void get_texture_data_size(const char (&name)[N], void *&data, int &size)
{
  auto texturefs = cmrc::textures::get_filesystem();
  auto tex = texturefs.open(name);
  data = (void *)tex.begin();
  size = (int)tex.size();
}

static double halfway(const points::render::aabb_t &aabb, int dimension)
{
  double aabb_width = aabb.max[dimension] - aabb.min[dimension];
  return aabb.min[dimension] + (aabb_width / 2);
}
static void get_aabb_center(const points::render::aabb_t &aabb, double (&center)[3])
{
  center[0] = halfway(aabb, 0);
  center[1] = halfway(aabb, 1);
  center[2] = halfway(aabb, 2);
}

int main(int, char **)
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    fmt::print(stderr, "could not initialize sdl video.");
    return -1;
  }
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 32);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  int width = 800;
  int height = 600;

  SDL_Window *window =
    SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                     SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_GLContext context = SDL_GL_CreateContext(window);

  if (!gladLoadGL())
  {
    fmt::print(stderr, "Failed to load opengl.");
    return 1;
  }

  SDL_GL_GetDrawableSize(window, &width, &height);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  const char *glsl_version = "#version 330";
  ImGui_ImplSDL2_InitForOpenGL(window, context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  auto fontsfs = cmrc::fonts::get_filesystem();

  io.Fonts->AddFontDefault();
  auto roboto = fontsfs.open("fonts/Roboto-Medium.ttf");
  io.Fonts->AddFontFromMemoryTTF((void *)roboto.begin(), (int)roboto.size(), 16.0f);

  auto cousine = fontsfs.open("fonts/Cousine-Regular.ttf");
  io.Fonts->AddFontFromMemoryTTF((void *)cousine.begin(), (int)cousine.size(), 15.0f);

  auto droidsans = fontsfs.open("fonts/DroidSans.ttf");
  io.Fonts->AddFontFromMemoryTTF((void *)droidsans.begin(), (int)droidsans.size(), 16.0f);

  auto proggy = fontsfs.open("fonts/ProggyTiny.ttf");
  io.Fonts->AddFontFromMemoryTTF((void *)proggy.begin(), (int)proggy.size(), 10.0f);

  // std::string file = "test.las";
  auto renderer = create_unique_ptr(points::render::renderer_create(), &points::render::renderer_destroy);
  auto camera = create_unique_ptr(points::render::camera_create(), &points::render::camera_destroy);
  gl_renderer points_gl_renderer(renderer.get(), camera.get());

  points::render::skybox_data_t skybox_data;
  get_texture_data_size("textures/right.jpg", skybox_data.positive_x, skybox_data.positive_x_size);
  get_texture_data_size("textures/left.jpg", skybox_data.negative_x, skybox_data.negative_x_size);
  get_texture_data_size("textures/top.jpg", skybox_data.positive_y, skybox_data.positive_y_size);
  get_texture_data_size("textures/bottom.jpg", skybox_data.negative_y, skybox_data.negative_y_size);
  get_texture_data_size("textures/front.jpg", skybox_data.positive_z, skybox_data.positive_z_size);
  get_texture_data_size("textures/back.jpg", skybox_data.negative_z, skybox_data.negative_z_size);
  auto skybox = create_unique_ptr(points::render::skybox_data_source_create(renderer.get(), skybox_data),
                                  &points::render::skybox_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::skybox_data_source_get(skybox.get()));

  auto aabb_ds = create_unique_ptr(points::render::aabb_data_source_create(renderer.get()),
                                   &points::render::aabb_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::aabb_data_source_get(aabb_ds.get()));
  points::render::aabb_t aabb;
  aabb.min[0] = -1.0; aabb.min[1] = -1.0; aabb.min[2] = -1.0;
  aabb.max[0] = 1.0; aabb.max[1] = 1.0; aabb.max[2] = 1.0;
  points::render::aabb_data_source_add_aabb(aabb_ds.get(), aabb.min, aabb.max);

  const char points_file[] = "D:/data/baerum_hoyde_laz/eksport_396769_20210126/124/data/32-1-512-133-65.laz";
  auto points = create_unique_ptr(points::render::flat_points_data_source_create(renderer.get(), points_file, sizeof(points_file)),
                                   &points::render::flat_points_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::flat_points_data_source_get(points.get()));


  points::render::flat_points_get_aabb(points.get(), aabb.min, aabb.max);
  (void)points;

  double aabb_center[3] = {5.0, 0.0, 5.0};
  
  double up[3];
  up[0] = 0.0; up[1] = 1.0; up[2] = 0.0;

  points::render::camera_set_perspective(camera.get(), 45, width, height, 0.1, 10000);
  points::render::camera_look_at_aabb(camera.get(), &aabb, aabb_center, up);

  points::render::aabb_t aabb2;
  aabb2.min[0] = 10.0; aabb2.min[1] = 1.5; aabb2.min[2] = 8.0;
  aabb2.max[0] = 10.5; aabb2.max[1] = 2.5; aabb2.max[2] = 8.9;
  int aabb_box2 = points::render::aabb_data_source_add_aabb(aabb_ds.get(), aabb2.min, aabb2.max);
  (void)aabb_box2;

  auto error = glGetError();
  (void)error;
  bool loop = true;
  bool left_pressed = false;
  bool right_pressed = false;
  bool ctrl_modifier = false;

  double arcball_center[3];
  get_aabb_center(aabb, arcball_center);
  auto arcball = create_unique_ptr(points::render::camera_manipulator::arcball_create(camera.get(), arcball_center), &points::render::camera_manipulator::arcball_destroy);
  auto fps = create_unique_ptr((points::render::camera_manipulator::fps_t *)nullptr, &points::render::camera_manipulator::fps_destroy);

  while(loop)
  {
    SDL_WaitEvent(nullptr);
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
        loop = false;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window))
        loop = false;
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (!(io.WantCaptureKeyboard && (event.type & 0x300))
        && !(io.WantCaptureMouse && (event.type & 0x400)))
      {
        switch (event.type)
        {
        case SDL_KEYDOWN:
          if (fps)
          {
            if (event.key.keysym.sym == SDLK_w || event.key.keysym.sym == SDLK_UP)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 0.0f, -1.3f);
            if (event.key.keysym.sym == SDLK_s || event.key.keysym.sym == SDLK_DOWN)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 0.0f, 1.3f);
            if (event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_LEFT)
              points::render::camera_manipulator::fps_move(fps.get(), -1.3f, 0.0f, 0.0f);
            if (event.key.keysym.sym == SDLK_d || event.key.keysym.sym == SDLK_RIGHT)
              points::render::camera_manipulator::fps_move(fps.get(), 1.3f, 0.0f, 0.0f);
            if (event.key.keysym.sym == SDLK_q)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, -1.3f, 0.0f);
            if (event.key.keysym.sym == SDLK_e)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 1.3f, 0.0f);
          }
          if (event.key.keysym.sym == SDLK_LCTRL || event.key.keysym.sym == SDLK_RCTRL)
            ctrl_modifier = true;
          break;
        case SDL_KEYUP:
          if (event.key.keysym.sym == SDLK_ESCAPE)
            loop = false;
          if (event.key.keysym.sym == SDLK_LCTRL || event.key.keysym.sym == SDLK_RCTRL)
            ctrl_modifier = false;
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = true;
            if (arcball)
              points::render::camera_manipulator::arcball_detect_upside_down(arcball.get());
            //if (arcball)
            //  points::render::camera_manipulator::arcball_reset(arcball.get());
            //else
            //  points::render::camera_manipulator::fps_reset(fps.get());
          }
          else if (event.button.button == SDL_BUTTON_RIGHT)
          {
            right_pressed = true;
          }
          break;
        case SDL_MOUSEMOTION:
          if ((right_pressed && !left_pressed) || (left_pressed && ctrl_modifier))
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = (float(event.motion.yrel) / float(height));
            float avg = (dx + dy) / 2;
            if (arcball)
              points::render::camera_manipulator::arcball_rotate(arcball.get(), 0.0f, 0.0f, avg);
            else if (fps)
              points::render::camera_manipulator::fps_rotate(fps.get(), 0.0f, 0.0f, avg);
          }
          else if (left_pressed)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = (float(event.motion.yrel) / float(height));
            if (arcball)
              points::render::camera_manipulator::arcball_rotate(arcball.get(), dx, dy, 0.0f);
            else
              points::render::camera_manipulator::fps_rotate(fps.get(), dx, dy, 0.0f);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = false;
          } else if (event.button.button == SDL_BUTTON_RIGHT)
          {
            right_pressed = false;
            if (arcball)
              points::render::camera_manipulator::arcball_reset(arcball.get());
            if (fps)
              points::render::camera_manipulator::fps_reset(fps.get());
          }
          break;
        case SDL_MOUSEWHEEL: 
          if (arcball && event.wheel.y)
          {
            points::render::camera_manipulator::arcball_zoom(arcball.get(), -float(event.wheel.y)/30); 
          }
          break;
        case SDL_WINDOWEVENT:
          switch (event.window.event)
          {
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            SDL_GL_GetDrawableSize(window, &width, &height);
            glViewport(0, 0, width, height);
            points::render::camera_set_perspective(camera.get(), 45, width, height, 0.01, 1000);
            break;
          }
          break;
        }
      }
    }

    clear clear_mask = clear(int(clear::color) | int(clear::depth));

    points_gl_renderer.draw(clear_mask, width, height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    ImGui::Begin("Input", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::BeginGroup();
    if (ImGui::RadioButton("ArcBall", arcball.get()))
    {
      if (!arcball)
      {
        fps.reset();
        arcball.reset(points::render::camera_manipulator::arcball_create(camera.get(), arcball_center));
      }
    }
    if (ImGui::RadioButton("FPS", fps.get()))
    {
      if (!fps)
      {
        arcball.reset();
        fps.reset(points::render::camera_manipulator::fps_create(camera.get()));
      }
    }
    ImGui::EndGroup();

    ImGui::End();
   
    //ImGui::ShowDemoWindow();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
    SDL_Delay(1);
  }

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
