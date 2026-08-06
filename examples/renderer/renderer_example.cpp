#include <fmt/printf.h>

#include "include/glad/glad.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <fmt/printf.h>
#include <stdio.h>

#include "gl_renderer.h"

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

// dewpp.hpp is the umbrella over the generated C++ wrapper; it pulls in the C headers it wraps, so
// the per-module includes those replaced are gone.
#include <dew/dewpp.hpp>

#include <dew/converter/converter.h>
#include <dew/converter/converter_data_source.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#define CMRC_NO_EXCEPTIONS 1
#include <cmrc/cmrc.hpp>

CMRC_DECLARE(fonts);

static double halfway(const dew_aabb_t &aabb, int dimension)
{
  double aabb_width = aabb.max[dimension] - aabb.min[dimension];
  return aabb.min[dimension] + (aabb_width / 2);
}
static std::array<double, 3> get_aabb_center(const dew_aabb_t &aabb)
{
  return {halfway(aabb, 0), halfway(aabb, 1), halfway(aabb, 2)};
}

template <size_t N>
dew_converter_str_buffer make_str_buffer(const char (&data)[N])
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

  SDL_Window *window = SDL_CreateWindow("dewfall", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  // dewpp is the generated C++ wrapper (bindings/cpp): the same C API with RAII handles, so there is
  // no create/destroy pairing to get wrong and no hand-rolled unique_ptr deleter. Creation returns
  // dewpp::result_t, which is how a failure reports itself with exceptions disabled.
  auto renderer_ = dewpp::renderer_t::create();
  auto camera_ = dewpp::camera_t::create();
  if (!renderer_ || !camera_)
  {
    fprintf(stderr, "Could not create the renderer/camera\n");
    return 1;
  }
  dewpp::renderer_t renderer = std::move(*renderer_);
  dewpp::camera_t camera = std::move(*camera_);
  gl_renderer dew_gl_renderer(renderer.handle(), camera.handle());

  dew_aabb_t aabb;

  bool render_converter_points = true;

  // Dataset selection. A dataset is either a local .dew file (a bare path or file://) or a cloud object
  // store addressed by URL: s3://bucket/prefix or az://container/prefix. Cloud credentials / endpoint /
  // region come from the connection string (semicolon-separated key=value; see vio connection_string.h),
  // e.g. "anonymous=true;region=eu-north-1" for a public S3 bucket, or "account=...;account_key=..." /
  // "account=...;sas=..." for Azure.
  //
  // CLI: renderer_example [url] [connection]. With no arguments the dialog below opens pre-filled with a
  // public sample dataset; a url on the command line loads straight away.
  // The public demo datasets. All three are anonymous-read in the same bucket, so one connection
  // string serves them and picking one is a single click -- no retyping a URL to try another.
  struct public_dataset_t
  {
    const char *label;
    const char *url;
  };
  static const public_dataset_t k_public_datasets[] = {
    {"Sw. Anny", "s3://limilind-public/points/g_sw_anny"},
    {"Kosciol Libusza", "s3://limilind-public/points/kosciol_libusza"},
    {"Palac Moszna", "s3://limilind-public/points/palac_moszna"},
  };
  static const char *k_default_url = k_public_datasets[0].url;
  static const char *k_default_connection = "anonymous=true;region=eu-north-1";

  char url_buf[1024];
  char conn_buf[1024];
  {
    std::string init_url = (argc > 1) ? argv[1] : k_default_url;
    std::string init_conn = (argc > 2) ? std::string(argv[2]) : (init_url == k_default_url ? std::string(k_default_connection) : std::string());
    snprintf(url_buf, sizeof(url_buf), "%s", init_url.c_str());
    snprintf(conn_buf, sizeof(conn_buf), "%s", init_conn.c_str());
  }

  dewpp::converter_data_source_t converter_points;
  std::string load_error;

  auto try_open = [&]() -> bool {
    std::string url = url_buf;
    std::string conn = conn_buf;
    // A public S3 bucket takes no credentials; default the connection to anonymous when the user left the
    // field empty. Azure and private buckets need an explicit connection string.
    if (conn.empty() && url.rfind("s3://", 0) == 0)
      conn = "anonymous=true";
    // The wrapper owns the dew_error_t: a failure comes back as the error value itself, so there is
    // no error handle to allocate, pass in, read out and free.
    auto opened = dewpp::converter_data_source_t::create_with_connection(url, conn, renderer);
    if (!opened)
    {
      load_error = opened.error().message();
      fprintf(stderr, "Failed to open dataset '%s': %d - %s\n", url.c_str(), opened.error().code(), load_error.c_str());
      converter_points.reset();
      return false;
    }
    converter_points = std::move(*opened);
    return true;
  };

  // The native file-open dialog is async; its captureless callback writes the chosen path straight into
  // url_buf (and clears the connection, since local files carry no credentials).
  struct browse_state_t
  {
    char *url_buf;
    size_t url_cap;
    char *conn_buf;
  };
  browse_state_t browse{url_buf, sizeof(url_buf), conn_buf};

  bool do_load = (argc > 1); // a url on the command line loads immediately; otherwise wait for the dialog
  while (true)
  {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT)
        return 0;
      if (ev.window.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(window))
        return 0;
    }

    if (do_load)
    {
      do_load = false;
      if (try_open())
        break;
    }

    SDL_GetWindowSizeInPixels(window, &width, &height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Open dataset", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted("Local .dew file, or a cloud object store:");
    ImGui::BulletText("s3://bucket/prefix");
    ImGui::BulletText("az://container/prefix");
    ImGui::PushItemWidth(440);
    ImGui::InputText("URL", url_buf, sizeof(url_buf));
    ImGui::InputTextWithHint("Connection", "anonymous=true;region=eu-north-1", conn_buf, sizeof(conn_buf));
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Cloud only. Public bucket: anonymous=true. Azure: account=...;account_key=... or ...;sas=...");
    ImGui::Spacing();
    ImGui::TextUnformatted("Public datasets:");
    for (const auto &dataset : k_public_datasets)
    {
      if (ImGui::Button(dataset.label))
      {
        snprintf(url_buf, sizeof(url_buf), "%s", dataset.url);
        snprintf(conn_buf, sizeof(conn_buf), "%s", k_default_connection);
        do_load = true;
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::Button("Browse local file..."))
    {
      SDL_DialogFileFilter filters[] = {{"Dewfall files", "dew"}};
      SDL_ShowOpenFileDialog(
        [](void *ud, const char *const *files, int) {
          auto *b = static_cast<browse_state_t *>(ud);
          if (files && *files)
          {
            snprintf(b->url_buf, b->url_cap, "%s", *files);
            b->conn_buf[0] = '\0';
          }
        },
        &browse, window, filters, 1, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open"))
      do_load = true;
    ImGui::SameLine();
    if (ImGui::Button("Quit"))
      return 0;
    if (!load_error.empty())
    {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to open:");
      ImGui::TextWrapped("%s", load_error.c_str());
    }
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  uint32_t attribute_count = dew_converter_data_attribute_count(converter_points.handle());
  std::vector<std::string> attribute_names;
  attribute_names.resize(attribute_count);
  {
    char buffer[256];
    for (uint32_t i = 0; i < attribute_count; i++)
    {
      auto str_size = dew_converter_data_get_attribute_name(converter_points.handle(), i, buffer, sizeof(buffer));
      attribute_names[i].assign(buffer, str_size);
    }
  }
  // Default colored attribute, matching the WebGL renderer's precedence (web/src/usePointCloudRenderer.ts):
  // 'rgb', then 'intensity', then the first attribute that isn't the coordinates ('xyz'), else the first
  // name ('xyz'). The attribute list places 'xyz' first. Matching is case-insensitive.
  auto pick_default_attribute = [](const std::vector<std::string> &names) -> int {
    auto ieq = [](const std::string &a, const char *b) {
      if (a.size() != std::strlen(b))
        return false;
      for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != (unsigned char)b[i])
          return false;
      return true;
    };
    int intensity = -1, first_non_xyz = -1;
    for (int i = 0; i < int(names.size()); i++)
    {
      if (ieq(names[i], "rgb"))
        return i;
      if (intensity < 0 && ieq(names[i], "intensity"))
        intensity = i;
      if (first_non_xyz < 0 && !ieq(names[i], "xyz"))
        first_non_xyz = i;
    }
    if (intensity >= 0)
      return intensity;
    if (first_non_xyz >= 0)
      return first_non_xyz;
    return 0;
  };
  int selected_attribute = pick_default_attribute(attribute_names);
  if (!attribute_names.empty())
  {
    auto &name = attribute_names[selected_attribute];
    dew_converter_data_set_rendered_attribute(converter_points.handle(), name.c_str(), uint32_t(name.size()));
  }
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
    dew_converter_data_source_request_aabb(converter_points.handle(), callback, &state);
    state.cv.wait(lock);
    memcpy(aabb.min, state.aabb_min, sizeof(state.aabb_min));
    memcpy(aabb.max, state.aabb_max, sizeof(state.aabb_max));
  }

  double ground_z = aabb.min[2];
  double grid_size = std::max({aabb.max[0] - aabb.min[0], aabb.max[1] - aabb.min[1]}) / 10.0;
  auto environment_ = dewpp::environment_data_source_t::create(renderer, ground_z, grid_size);
  if (!environment_)
  {
    fprintf(stderr, "Could not create the environment data source\n");
    return 1;
  }
  dewpp::environment_data_source_t environment = std::move(*environment_);
  dew_renderer_add_data_source(renderer.handle(), dew_environment_data_source_get(environment.handle()));

  float screen_fraction_threshold = 0.65f;
  float render_density_px = 0.8f;
  // 512 MB (was 64): a bigger resident budget cuts the eviction/re-upload churn that a low budget forces on
  // every camera move -- the main lever, together with non-blocking eviction, against interaction stutter.
  int gpu_memory_budget_mb = 512;
  // Streaming is network-latency-bound (each blob is a separate GET), so more concurrent reads hide latency.
  int max_in_flight_io = 128;
  bool show_bounding_boxes = false;
  bool debug_transitions = false;

  dew_renderer_add_data_source(renderer.handle(), dew_converter_data_source_get(converter_points.handle()));
  dew_renderer_add_data_source(renderer.handle(), dew_converter_data_source_get_bbox_data_source(converter_points.handle()));
  dew_converter_data_source_set_viewport(converter_points.handle(), width, height);
  dew_converter_data_source_set_gpu_memory_budget(converter_points.handle(), size_t(gpu_memory_budget_mb) * 1024 * 1024);
  dew_converter_data_source_set_max_in_flight_io(converter_points.handle(), max_in_flight_io);

  std::vector<uint32_t> storage_ids;
  std::vector<uint32_t> storage_subs;
  std::vector<std::string> storage_strings;
  // Drawing the node boxes as a separate source, kept for reference:
  // dewpp::aabb_data_source_t aabb_ds = std::move(*dewpp::aabb_data_source_t::create(renderer));
  // renderer.add_data_source(aabb_ds.get());
  // aabb_ds.add_aabb(aabb.min, aabb.max);

  double view_direction[3] = {0.0, -1.0, 0.0};
  double up[3] = {0.0, 0.0, 1.0};
  double z_up[3] = {0.0, 0.0, 1.0};

  dew_camera_set_perspective(camera.handle(), 45, width, height, 0.1, 100000);
  dew_camera_look_at_aabb(camera.handle(), &aabb, view_direction, up);

  dew_aabb_t aabb2;
  aabb2.min[0] = 0.0;
  aabb2.min[1] = 0.0;
  aabb2.min[2] = 0.0;
  aabb2.max[0] = 0.0;
  aabb2.max[1] = 0.0;
  aabb2.max[2] = 0.0;
  // int aabb2_id =  -1; //dew_flat_points_aabb_data_source_add_aabb(aabb_ds.get(), aabb.min, aabb.max);

  bool loop = true;
  bool left_pressed = false;
  bool right_pressed = false;
  bool middle_pressed = false;
  bool ctrl_modifier = false;
  bool shift_modifier = false;

  // std::array rather than double[3]: the wrapper takes fixed-size arrays by reference, which is what
  // makes the length part of the type instead of a convention.
  std::array<double, 3> arcball_center = get_aabb_center(aabb);
  dewpp::arcball_t arcball = std::move(*dewpp::arcball_t::create(camera, arcball_center));
  dew_arcball_set_up_axis(arcball.handle(), z_up);
  dewpp::fps_t fps;

  // Swap the rendered dataset in place.
  //
  // Everything downstream of the data source has to be rebuilt, not just the source: the attribute
  // list, the extent (which the environment grid and the camera framing are derived from) and the
  // arcball centre all belong to the dataset. Detaching the old sources from the renderer FIRST
  // matters -- dew_converter_data_source_destroy frees the GPU buffers those draw groups name, and a
  // renderer still holding them would hand the consumer dead handles on the next frame.
  auto switch_dataset = [&](const char *url, const char *connection) {
    snprintf(url_buf, sizeof(url_buf), "%s", url);
    snprintf(conn_buf, sizeof(conn_buf), "%s", connection);

    dew_renderer_remove_data_source(renderer.handle(), dew_converter_data_source_get(converter_points.handle()));
    dew_renderer_remove_data_source(renderer.handle(), dew_converter_data_source_get_bbox_data_source(converter_points.handle()));
    dew_renderer_remove_data_source(renderer.handle(), dew_environment_data_source_get(environment.handle()));
    converter_points.reset();

    if (!try_open())
      return false;

    attribute_count = dew_converter_data_attribute_count(converter_points.handle());
    attribute_names.assign(attribute_count, std::string());
    {
      char buffer[256];
      for (uint32_t i = 0; i < attribute_count; i++)
      {
        auto str_size = dew_converter_data_get_attribute_name(converter_points.handle(), i, buffer, sizeof(buffer));
        attribute_names[i].assign(buffer, str_size);
      }
    }
    selected_attribute = pick_default_attribute(attribute_names);
    if (!attribute_names.empty())
    {
      auto &name = attribute_names[selected_attribute];
      dew_converter_data_set_rendered_attribute(converter_points.handle(), name.c_str(), uint32_t(name.size()));
    }

    dew_converter_data_source_get_tight_aabb(converter_points.handle(), aabb.min, aabb.max);

    auto rebuilt = dewpp::environment_data_source_t::create(renderer, aabb.min[2], std::max({aabb.max[0] - aabb.min[0], aabb.max[1] - aabb.min[1]}) / 10.0);
    if (rebuilt)
      environment = std::move(*rebuilt);

    dew_renderer_add_data_source(renderer.handle(), dew_environment_data_source_get(environment.handle()));
    dew_renderer_add_data_source(renderer.handle(), dew_converter_data_source_get(converter_points.handle()));
    dew_renderer_add_data_source(renderer.handle(), dew_converter_data_source_get_bbox_data_source(converter_points.handle()));
    dew_converter_data_source_set_viewport(converter_points.handle(), width, height);
    dew_converter_data_source_set_gpu_memory_budget(converter_points.handle(), size_t(gpu_memory_budget_mb) * 1024 * 1024);
    dew_converter_data_source_set_max_in_flight_io(converter_points.handle(), max_in_flight_io);
    dew_converter_data_source_set_show_bounding_boxes(converter_points.handle(), show_bounding_boxes ? 1 : 0);

    // Re-frame: the new dataset is somewhere else entirely, so keeping the old camera would look like
    // a failed load.
    dew_camera_look_at_aabb(camera.handle(), &aabb, view_direction, up);
    arcball_center = get_aabb_center(aabb);
    if (arcball)
    {
      arcball = std::move(*dewpp::arcball_t::create(camera, arcball_center));
      dew_arcball_set_up_axis(arcball.handle(), z_up);
    }
    return true;
  };

  double dx_aabb = aabb.max[0] - aabb.min[0], dy_aabb = aabb.max[1] - aabb.min[1], dz_aabb = aabb.max[2] - aabb.min[2];
  double gizmo_length = std::sqrt(dx_aabb * dx_aabb + dy_aabb * dy_aabb + dz_aabb * dz_aabb) * 0.05;
  dewpp::axis_gizmo_data_source_t axis_gizmo = std::move(*dewpp::axis_gizmo_data_source_t::create(renderer, arcball_center, gizmo_length));
  dew_renderer_add_data_source(renderer.handle(), dew_axis_gizmo_data_source_get(axis_gizmo.handle()));

  dewpp::origin_anchor_data_source_t origin_anchor = std::move(*dewpp::origin_anchor_data_source_t::create(renderer, arcball_center, 1.0));
  dew_renderer_add_data_source(renderer.handle(), dew_origin_anchor_data_source_get(origin_anchor.handle()));

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
              dew_fps_move(fps.handle(), 0.0f, 0.0f, -1.3f);
            if (event.key.key == SDLK_S || event.key.key == SDLK_DOWN)
              dew_fps_move(fps.handle(), 0.0f, 0.0f, 1.3f);
            if (event.key.key == SDLK_A || event.key.key == SDLK_LEFT)
              dew_fps_move(fps.handle(), -1.3f, 0.0f, 0.0f);
            if (event.key.key == SDLK_D || event.key.key == SDLK_RIGHT)
              dew_fps_move(fps.handle(), 1.3f, 0.0f, 0.0f);
            if (event.key.key == SDLK_Q)
              dew_fps_move(fps.handle(), 0.0f, -1.3f, 0.0f);
            if (event.key.key == SDLK_E)
              dew_fps_move(fps.handle(), 0.0f, 1.3f, 0.0f);
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
              dew_arcball_dolly(arcball.handle(), dy);
          }
          else if (right_pressed && !left_pressed && ctrl_modifier)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              dew_arcball_pan_ground(arcball.handle(), dx, dy);
          }
          else if (middle_pressed || (right_pressed && !left_pressed))
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              dew_arcball_pan(arcball.handle(), dx, dy);
          }
          else if (left_pressed && ctrl_modifier)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            float avg = (dx + dy) / 2;
            if (arcball)
              dew_arcball_rotate(arcball.handle(), 0.0f, 0.0f, avg);
            else if (fps)
              dew_fps_rotate(fps.handle(), 0.0f, 0.0f, avg);
          }
          else if (left_pressed)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = -(float(event.motion.yrel) / float(height));
            if (arcball)
              dew_arcball_rotate(arcball.handle(), dx, dy, 0.0f);
            else
              dew_fps_rotate(fps.handle(), dx, dy, 0.0f);
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
            dew_arcball_zoom(arcball.handle(), -float(event.wheel.y) / 30);
          }
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          SDL_GetWindowSizeInPixels(window, &width, &height);
          glViewport(0, 0, width, height);
          dew_camera_set_perspective(camera.handle(), 45, width, height, 0.1, 100000);
          if (converter_points)
            dew_converter_data_source_set_viewport(converter_points.handle(), width, height);
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
      dew_arcball_get_center(arcball.handle(), gizmo_center);
      if (axis_gizmo)
        dew_axis_gizmo_data_source_set_center(axis_gizmo.handle(), gizmo_center);
      if (origin_anchor)
        dew_origin_anchor_data_source_set_center(origin_anchor.handle(), gizmo_center);
    }

    clear clear_mask = clear(int(clear::color) | int(clear::depth));

    dew_gl_renderer.draw(clear_mask, width, height);

    {
      double tight_min[3], tight_max[3];
      dew_converter_data_source_get_tight_aabb(converter_points.handle(), tight_min, tight_max);
      if (tight_min[2] < ground_z)
      {
        ground_z = tight_min[2];
        dew_environment_data_source_set_ground_z(environment.handle(), ground_z);
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Input", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushItemWidth(200);

    // Dataset knob: switch between the public demos without restarting.
    ImGui::TextUnformatted("Dataset");
    for (const auto &dataset : k_public_datasets)
    {
      const bool active = std::strcmp(url_buf, dataset.url) == 0;
      if (active)
        ImGui::BeginDisabled();
      if (ImGui::Button(dataset.label))
      {
        if (!switch_dataset(dataset.url, k_default_connection))
          fprintf(stderr, "Failed to switch dataset: %s\n", load_error.c_str());
      }
      if (active)
        ImGui::EndDisabled();
      ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextDisabled("%s", url_buf);
    ImGui::Separator();

    if (ImGui::RadioButton("ArcBall", arcball.handle()))
    {
      if (!arcball)
      {
        fps.reset();
        double eye[3], fwd[3];
        dew_camera_get_eye(camera.handle(), eye);
        dew_camera_get_forward(camera.handle(), fwd);
        double dx = aabb.max[0] - aabb.min[0], dy = aabb.max[1] - aabb.min[1], dz = aabb.max[2] - aabb.min[2];
        double orbit_dist = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5;
        std::array<double, 3> new_center{eye[0] + fwd[0] * orbit_dist, eye[1] + fwd[1] * orbit_dist, eye[2] + fwd[2] * orbit_dist};
        arcball = std::move(*dewpp::arcball_t::create(camera, new_center));
        dew_arcball_set_up_axis(arcball.handle(), z_up);
      }
    }
    if (ImGui::RadioButton("FPS", fps.handle()))
    {
      if (!fps)
      {
        arcball.reset();
        fps = std::move(*dewpp::fps_t::create(camera));
      }
    }
    // if (ImGui::Checkbox("Render flat", &render_flat_points))
    //{
    //   if (render_flat_points)
    //   {
    //     dew_renderer_add_data_source(renderer.handle(), dew_flat_points_data_source_get(dew_flat_points.get()));
    //   }
    //   else
    //   {
    //     dew_renderer_remove_data_source(renderer.handle(), dew_flat_points_data_source_get(dew_flat_points.get()));
    //   }
    // }
    if (ImGui::Checkbox("Render converter", &render_converter_points))
    {
      if (render_converter_points)
      {
        dew_renderer_add_data_source(renderer.handle(), dew_converter_data_source_get(converter_points.handle()));
      }
      else
      {
        dew_renderer_remove_data_source(renderer.handle(), dew_converter_data_source_get(converter_points.handle()));
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
          dew_converter_data_set_rendered_attribute(converter_points.handle(), name.c_str(), uint32_t(name.size()));
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
      dew_converter_data_source_set_pixel_error_threshold(converter_points.handle(), double(screen_fraction_threshold));
    }
    if (ImGui::SliderFloat("Render Density (px)", &render_density_px, 0.5f, 6.0f, "%.1f"))
    {
      dew_converter_data_source_set_render_density_px(converter_points.handle(), double(render_density_px));
    }
    if (ImGui::SliderInt("GPU Memory Budget (MB)", &gpu_memory_budget_mb, 64, 4096))
    {
      dew_converter_data_source_set_gpu_memory_budget(converter_points.handle(), size_t(gpu_memory_budget_mb) * 1024 * 1024);
    }
    if (ImGui::SliderInt("Max In-Flight IO", &max_in_flight_io, 8, 512))
    {
      dew_converter_data_source_set_max_in_flight_io(converter_points.handle(), max_in_flight_io);
    }
    if (ImGui::Checkbox("Show Bounding Boxes", &show_bounding_boxes))
    {
      dew_converter_data_source_set_show_bounding_boxes(converter_points.handle(), show_bounding_boxes);
    }
    if (ImGui::Checkbox("Debug Transitions", &debug_transitions))
    {
      dew_converter_data_source_set_debug_transitions(converter_points.handle(), debug_transitions);
    }
    ImGui::SliderFloat("Point World Size", &dew_gl_renderer.point_world_size, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("LOD Scale Base", &dew_gl_renderer.lod_scale_base, 1.0f, 5.0f, "%.1f");
    {
      uint64_t points_rendered = dew_converter_data_source_get_points_rendered(converter_points.handle());
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
      int walker_nodes, walker_trees_pending, io_in_flight;
      uint64_t walker_total_pts;
      dew_converter_data_source_get_frame_timings(converter_points.handle(), &tree_walk, &reconciliation, &upload, &refine, &frontier, &draw, &eviction, &total,
                                                                  &registry_nodes, &active_set, &nodes_drawn, &transitioning, &evicted, &reconcile_destroyed,
                                                                  &walker_nodes, &walker_total_pts, &walker_trees_pending, &io_in_flight);
      ImGui::Text("Total:          %.2f ms", total);
      ImGui::Text("Tree Walk:      %.2f ms", tree_walk);
      ImGui::Text("Reconciliation: %.2f ms", reconciliation);
      ImGui::Text("GPU Upload:     %.2f ms", upload);
      ImGui::Text("Refine:         %.2f ms", refine);
      ImGui::Text("Frontier I/O:   %.2f ms", frontier);
      ImGui::Text("Draw Emission:  %.2f ms", draw);
      ImGui::Text("Eviction:       %.2f ms", eviction);
      ImGui::Separator();
      ImGui::Text("Walker Nodes:    %d", walker_nodes);
      ImGui::Text("Walker Points:   %.2f M", double(walker_total_pts) / 1000000.0);
      ImGui::Text("Trees Pending:   %d", walker_trees_pending);
      ImGui::Text("Registry Nodes:  %d", registry_nodes);
      ImGui::Text("Active Set:      %d", active_set);
      ImGui::Text("Nodes Drawn:     %d", nodes_drawn);
      ImGui::Text("IO In-Flight:    %d", io_in_flight);
      if (debug_transitions)
      {
        ImGui::Separator();
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
