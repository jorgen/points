#include <fmt/printf.h>

#include "include/glad/glad.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <fmt/printf.h>
#include <stdio.h>

#include "gl_renderer.h"

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

#include <points/render/aabb.h>
#include <points/render/camera.h>
#include <points/render/renderer.h>
#include <points/render/environment_data_source.h>
#include <points/render/axis_gizmo_data_source.h>
#include <points/render/origin_anchor_data_source.h>

#include <points/converter/converter.h>
#include <points/converter/converter_data_source.h>

#include <cmath>
#include <string>
#include <vector>

#define CMRC_NO_EXCEPTIONS 1
#include <cmrc/cmrc.hpp>

CMRC_DECLARE(fonts);

template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> create_unique_ptr(T *t, Deleter d)
{
  return std::unique_ptr<T, Deleter>(t, d);
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

template <size_t N>
points::converter::str_buffer make_str_buffer(const char (&data)[N])
{
  return {data, N};
}

int main(int argc, char **argv)
{
  if (!SDL_Init(SDL_INIT_VIDEO))
  {
    fmt::print(stderr, "could not initialize sdl video.");
    return -1;
  }
  if (!SDL_GL_LoadLibrary(nullptr))
  {
    fmt::print(stderr, "Failed to load opengl library");
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

  SDL_Window *window = SDL_CreateWindow("points", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window)
  {
    fmt::print(stderr, "Failed to create window.");
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context)
  {
    fmt::print(stderr, "Failed to create context.");
  }
  if (!SDL_GL_MakeCurrent(window, context))
  {
    fmt::print(stderr, "Failed to make current context active.");
  }

  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
  {
    fmt::print(stderr, "Failed to load opengl.");
    return 1;
  }

  SDL_GetWindowSizeInPixels(window, &width, &height);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  const char *glsl_version = "#version 330";
  ImGui_ImplSDL3_InitForOpenGL(window, context);
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

  std::string cache_path;
  if (argc > 1)
  {
    cache_path = argv[1];
  }
  else
  {
    struct dialog_state
    {
      std::string result;
      bool done = false;
    };
    dialog_state state;
    SDL_DialogFileFilter filters[] = {{"Points files", "jlp"}};
    SDL_ShowOpenFileDialog(
      [](void *ud, const char *const *files, int) {
        auto *s = static_cast<dialog_state *>(ud);
        if (files && *files)
          s->result = *files;
        s->done = true;
      },
      &state, window, filters, 1, nullptr, false);
    while (!state.done)
    {
      SDL_Event ev;
      while (SDL_PollEvent(&ev))
      {
        if (ev.type == SDL_EVENT_QUIT)
          return 0;
      }
      SDL_Delay(10);
    }
    if (state.result.empty())
      return 0;
    cache_path = state.result;
  }

  points::render::aabb_t aabb;

  auto error = create_unique_ptr(points::error_create(), &points::error_destroy);
  bool render_converter_points = true;
  auto converter_points = create_unique_ptr(points::converter::converter_data_source_create(cache_path.c_str(), uint32_t(cache_path.size()), error.get(), renderer.get()), &points::converter::converter_data_source_destroy);
  if (!converter_points)
  {
    int code;
    const char *str;
    size_t str_len;
    points::error_get_info(error.get(), &code, &str, &str_len);
    fprintf(stderr, "Failed to create converter_data_source: %d - %s\n", code, str);
    exit(1);
  }

  uint32_t attribute_count = points::converter::converter_data_attribute_count(converter_points.get());
  std::vector<std::string> attribute_names;
  attribute_names.resize(attribute_count);
  {
    char buffer[256];
    for (uint32_t i = 0; i < attribute_count; i++)
    {
      auto str_size = points::converter::converter_data_get_attribute_name(converter_points.get(), i, buffer, sizeof(buffer));
      attribute_names[i].assign(buffer, str_size);
    }
  }
  int selected_attribute = 1;
  {
    struct aabb_callback_state_t
    {
      std::mutex wait;
      std::condition_variable cv;
      double aabb_min[3];
      double aabb_max[3];
    };

    aabb_callback_state_t state;
    std::unique_lock<std::mutex> lock(state.wait);
    auto callback = [](double aabb_min[3], double aabb_max[3], void *user_ptr)
    {
      auto state = (aabb_callback_state_t *)user_ptr;
      memcpy(state->aabb_min, aabb_min, sizeof(state->aabb_min));
      memcpy(state->aabb_max, aabb_max, sizeof(state->aabb_max));
      state->cv.notify_one();
    };
    points::converter::converter_data_source_request_aabb(converter_points.get(), callback, &state);
    state.cv.wait(lock);
    memcpy(aabb.min, state.aabb_min, sizeof(state.aabb_min));
    memcpy(aabb.max, state.aabb_max, sizeof(state.aabb_max));
  }

  double ground_z = aabb.min[2];
  double grid_size = std::max({aabb.max[0] - aabb.min[0], aabb.max[1] - aabb.min[1]}) / 10.0;
  auto environment = create_unique_ptr(
    points::render::environment_data_source_create(renderer.get(), ground_z, grid_size),
    &points::render::environment_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::environment_data_source_get(environment.get()));

  float screen_fraction_threshold = 0.5f;
  int gpu_memory_budget_mb = 64;
  bool show_bounding_boxes = false;
  bool debug_transitions = false;

  points::render::renderer_add_data_source(renderer.get(), points::converter::converter_data_source_get(converter_points.get()));
  points::render::renderer_add_data_source(renderer.get(), points::converter::converter_data_source_get_bbox_data_source(converter_points.get()));
  points::converter::converter_data_source_set_viewport(converter_points.get(), width, height);
  points::converter::converter_data_source_set_gpu_memory_budget(converter_points.get(), size_t(gpu_memory_budget_mb) * 1024 * 1024);

  std::vector<uint32_t> storage_ids;
  std::vector<uint32_t> storage_subs;
  std::vector<std::string> storage_strings;
  // auto aabb_ds = create_unique_ptr(flat_points::render::aabb_data_source_create(renderer.get(), aabb.min),
  //                                  &flat_points::render::aabb_data_source_destroy);
  // flat_points::render::renderer_add_data_source(renderer.get(), flat_points::render::aabb_data_source_get(aabb_ds.get()));
  // flat_points::render::aabb_data_source_add_aabb(aabb_ds.get(), aabb.min, aabb.max);
  // flat_points::render::aabb_data_source_add_aabb(aabb_ds.get(), aabb.min, aabb.max);
  //(void)flat_points;

  double view_direction[3] = {0.0, -1.0, 0.0};
  double up[3] = {0.0, 0.0, 1.0};
  double z_up[3] = {0.0, 0.0, 1.0};

  points::render::camera_set_perspective(camera.get(), 45, width, height, 0.1, 100000);
  points::render::camera_look_at_aabb(camera.get(), &aabb, view_direction, up);

  points::render::aabb_t aabb2;
  aabb2.min[0] = 0.0;
  aabb2.min[1] = 0.0;
  aabb2.min[2] = 0.0;
  aabb2.max[0] = 0.0;
  aabb2.max[1] = 0.0;
  aabb2.max[2] = 0.0;
  // int aabb2_id =  -1; //flat_points::render::aabb_data_source_add_aabb(aabb_ds.get(), aabb.min, aabb.max);

  bool loop = true;
  bool left_pressed = false;
  bool right_pressed = false;
  bool middle_pressed = false;
  bool ctrl_modifier = false;
  bool shift_modifier = false;

  double arcball_center[3];
  get_aabb_center(aabb, arcball_center);
  auto arcball = create_unique_ptr(points::render::camera_manipulator::arcball_create(camera.get(), arcball_center), &points::render::camera_manipulator::arcball_destroy);
  points::render::camera_manipulator::arcball_set_up_axis(arcball.get(), z_up);
  auto fps = create_unique_ptr((points::render::camera_manipulator::fps_t *)nullptr, &points::render::camera_manipulator::fps_destroy);

  double dx_aabb = aabb.max[0] - aabb.min[0], dy_aabb = aabb.max[1] - aabb.min[1], dz_aabb = aabb.max[2] - aabb.min[2];
  double gizmo_length = std::sqrt(dx_aabb * dx_aabb + dy_aabb * dy_aabb + dz_aabb * dz_aabb) * 0.05;
  auto axis_gizmo = create_unique_ptr(points::render::axis_gizmo_data_source_create(renderer.get(), arcball_center, gizmo_length), &points::render::axis_gizmo_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::axis_gizmo_data_source_get(axis_gizmo.get()));

  auto origin_anchor = create_unique_ptr(points::render::origin_anchor_data_source_create(renderer.get(), arcball_center, 1.0), &points::render::origin_anchor_data_source_destroy);
  points::render::renderer_add_data_source(renderer.get(), points::render::origin_anchor_data_source_get(origin_anchor.get()));

  while (loop)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
        loop = false;
      if (event.window.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
        loop = false;
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (!(io.WantCaptureKeyboard && (event.type & 0x300)) && !(io.WantCaptureMouse && (event.type & 0x400)))
      {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
          if (fps)
          {
            if (event.key.key == SDLK_W || event.key.key == SDLK_UP)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 0.0f, -1.3f);
            if (event.key.key == SDLK_S || event.key.key == SDLK_DOWN)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 0.0f, 1.3f);
            if (event.key.key == SDLK_A || event.key.key == SDLK_LEFT)
              points::render::camera_manipulator::fps_move(fps.get(), -1.3f, 0.0f, 0.0f);
            if (event.key.key == SDLK_D || event.key.key == SDLK_RIGHT)
              points::render::camera_manipulator::fps_move(fps.get(), 1.3f, 0.0f, 0.0f);
            if (event.key.key == SDLK_Q)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, -1.3f, 0.0f);
            if (event.key.key == SDLK_E)
              points::render::camera_manipulator::fps_move(fps.get(), 0.0f, 1.3f, 0.0f);
          }

          if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL)
            ctrl_modifier = true;
          if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT)
            shift_modifier = true;
          break;
        case SDL_EVENT_KEY_UP:
          if (event.key.key == SDLK_ESCAPE)
            loop = false;
          if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL)
            ctrl_modifier = false;
          if (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT)
            shift_modifier = false;
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = true;
          }
          else if (event.button.button == SDL_BUTTON_MIDDLE)
          {
            middle_pressed = true;
          }
          else if (event.button.button == SDL_BUTTON_RIGHT)
          {
            right_pressed = true;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION:
          if (right_pressed && !left_pressed && shift_modifier)
          {
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              points::render::camera_manipulator::arcball_dolly(arcball.get(), dy);
          }
          else if (right_pressed && !left_pressed && ctrl_modifier)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              points::render::camera_manipulator::arcball_pan_ground(arcball.get(), dx, dy);
          }
          else if (middle_pressed || (right_pressed && !left_pressed))
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              points::render::camera_manipulator::arcball_pan(arcball.get(), dx, dy);
          }
          else if (left_pressed && ctrl_modifier)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            float avg = (dx + dy) / 2;
            if (arcball)
              points::render::camera_manipulator::arcball_rotate(arcball.get(), 0.0f, 0.0f, avg);
            else if (fps)
              points::render::camera_manipulator::fps_rotate(fps.get(), 0.0f, 0.0f, avg);
          }
          else if (left_pressed)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              points::render::camera_manipulator::arcball_rotate(arcball.get(), dx, dy, 0.0f);
            else
              points::render::camera_manipulator::fps_rotate(fps.get(), dx, dy, 0.0f);
          }
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = false;
          }
          else if (event.button.button == SDL_BUTTON_MIDDLE)
          {
            middle_pressed = false;
          }
          else if (event.button.button == SDL_BUTTON_RIGHT)
          {
            right_pressed = false;
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          if (arcball && event.wheel.y)
          {
            points::render::camera_manipulator::arcball_zoom(arcball.get(), -float(event.wheel.y) / 30);
          }
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          SDL_GetWindowSizeInPixels(window, &width, &height);
          glViewport(0, 0, width, height);
          points::render::camera_set_perspective(camera.get(), 45, width, height, 0.1, 100000);
          if (converter_points)
            points::converter::converter_data_source_set_viewport(converter_points.get(), width, height);
          break;
        }
        default:
          break;
        }
      }
    }

    if (arcball)
    {
      double gizmo_center[3];
      points::render::camera_manipulator::arcball_get_center(arcball.get(), gizmo_center);
      if (axis_gizmo)
        points::render::axis_gizmo_data_source_set_center(axis_gizmo.get(), gizmo_center);
      if (origin_anchor)
        points::render::origin_anchor_data_source_set_center(origin_anchor.get(), gizmo_center);
    }

    clear clear_mask = clear(int(clear::color) | int(clear::depth));

    points_gl_renderer.draw(clear_mask, width, height);
    // double converter_min[3];
    // double converter_max[3];
    // flat_points::converter::converter_data_source_get_aabb(converter_points.get(), converter_min, converter_max);
    //  if (converter_min[0] != aabb2.min[0] && aabb2_id == -1)
    //  {
    //    aabb2_id = flat_points::render::aabb_data_source_add_aabb(aabb_ds.get(), converter_min, converter_max);
    //    memcpy(aabb2.min, converter_min, sizeof(converter_min));
    //    memcpy(aabb2.max, converter_max, sizeof(converter_max));
    //  }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Input", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushItemWidth(200);
    if (ImGui::RadioButton("ArcBall", arcball.get()))
    {
      if (!arcball)
      {
        fps.reset();
        double eye[3], fwd[3];
        points::render::camera_get_eye(camera.get(), eye);
        points::render::camera_get_forward(camera.get(), fwd);
        double dx = aabb.max[0] - aabb.min[0], dy = aabb.max[1] - aabb.min[1], dz = aabb.max[2] - aabb.min[2];
        double orbit_dist = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
        double new_center[3] = {eye[0] + fwd[0] * orbit_dist, eye[1] + fwd[1] * orbit_dist, eye[2] + fwd[2] * orbit_dist};
        arcball.reset(points::render::camera_manipulator::arcball_create(camera.get(), new_center));
        points::render::camera_manipulator::arcball_set_up_axis(arcball.get(), z_up);
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
    // if (ImGui::Checkbox("Render flat", &render_flat_points))
    //{
    //   if (render_flat_points)
    //   {
    //     points::render::renderer_add_data_source(renderer.get(), points::render::flat_points_data_source_get(flat_points.get()));
    //   }
    //   else
    //   {
    //     points::render::renderer_remove_data_source(renderer.get(), points::render::flat_points_data_source_get(flat_points.get()));
    //   }
    // }
    if (ImGui::Checkbox("Render converter", &render_converter_points))
    {
      if (render_converter_points)
      {
        points::render::renderer_add_data_source(renderer.get(), points::converter::converter_data_source_get(converter_points.get()));
      }
      else
      {
        points::render::renderer_remove_data_source(renderer.get(), points::converter::converter_data_source_get(converter_points.get()));
      }
    }
    if (ImGui::BeginCombo("Attribute", attribute_names[selected_attribute].c_str()))
    {
      for (int i = 0; i < int(attribute_names.size()); i++)
      {
        bool is_selected = (selected_attribute == i);
        if (ImGui::Selectable(attribute_names[i].c_str(), is_selected))
        {
          selected_attribute = i;
          auto &name = attribute_names[selected_attribute];
          points::converter::converter_data_set_rendered_attribute(converter_points.get(), name.c_str(), uint32_t(name.size()));
        }
        if (is_selected)
        {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::SliderFloat("Screen Fraction Threshold", &screen_fraction_threshold, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
      points::converter::converter_data_source_set_pixel_error_threshold(converter_points.get(), double(screen_fraction_threshold));
    }
    if (ImGui::SliderInt("GPU Memory Budget (MB)", &gpu_memory_budget_mb, 64, 4096))
    {
      points::converter::converter_data_source_set_gpu_memory_budget(converter_points.get(), size_t(gpu_memory_budget_mb) * 1024 * 1024);
    }
    if (ImGui::Checkbox("Show Bounding Boxes", &show_bounding_boxes))
    {
      points::converter::converter_data_source_set_show_bounding_boxes(converter_points.get(), show_bounding_boxes);
    }
    if (ImGui::Checkbox("Debug Transitions", &debug_transitions))
    {
      points::converter::converter_data_source_set_debug_transitions(converter_points.get(), debug_transitions);
    }
    ImGui::SliderFloat("Point World Size", &points_gl_renderer.point_world_size, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("LOD Scale Base", &points_gl_renderer.lod_scale_base, 1.0f, 5.0f, "%.1f");
    {
      uint64_t points_rendered = points::converter::converter_data_source_get_points_rendered(converter_points.get());
      if (points_rendered >= 1000000)
        ImGui::Text("Points Rendered: %.2f M", double(points_rendered) / 1000000.0);
      else if (points_rendered >= 1000)
        ImGui::Text("Points Rendered: %.1f K", double(points_rendered) / 1000.0);
      else
        ImGui::Text("Points Rendered: %llu", (unsigned long long)points_rendered);
    }
    if (ImGui::CollapsingHeader("Frame Timings"))
    {
      double tree_walk, reconciliation, upload, refine, frontier, draw, eviction, total;
      int registry_nodes, active_set, nodes_drawn, transitioning, evicted, reconcile_destroyed;
      points::converter::converter_data_source_get_frame_timings(converter_points.get(), &tree_walk, &reconciliation, &upload, &refine, &frontier, &draw, &eviction, &total,
                                                                  &registry_nodes, &active_set, &nodes_drawn, &transitioning, &evicted, &reconcile_destroyed);
      ImGui::Text("Total:          %.2f ms", total);
      ImGui::Text("Tree Walk:      %.2f ms", tree_walk);
      ImGui::Text("Reconciliation: %.2f ms", reconciliation);
      ImGui::Text("GPU Upload:     %.2f ms", upload);
      ImGui::Text("Refine:         %.2f ms", refine);
      ImGui::Text("Frontier I/O:   %.2f ms", frontier);
      ImGui::Text("Draw Emission:  %.2f ms", draw);
      ImGui::Text("Eviction:       %.2f ms", eviction);
      if (debug_transitions)
      {
        ImGui::Separator();
        ImGui::Text("Registry Nodes:  %d", registry_nodes);
        ImGui::Text("Active Set:      %d", active_set);
        ImGui::Text("Transitioning:   %d", transitioning);
        ImGui::Text("Evicted:         %d", evicted);
        ImGui::Text("Reconcile Dest:  %d", reconcile_destroyed);
      }
    }
    ImGui::PopItemWidth();
    ImGui::End();

    // ImGui::ShowDemoWindow();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
