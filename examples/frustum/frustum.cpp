#include <fmt/printf.h>

#include <SDL.h>
#include "include/glad/glad.h"
#include <fmt/printf.h>
#include <stdio.h>

#pragma warning(push)
#pragma warning(disable : 4201 )
#pragma warning(disable : 4127 )  
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include <examples/imgui_impl_sdl.h>
#include <examples/imgui_impl_opengl3.h>

#include <points/render/renderer.h>
#include <points/render/camera.h>
#include <points/render/aabb.h>

#include <vector>

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(shaders);
CMRC_DECLARE(fonts);

struct shader_deleter
{
  shader_deleter(GLuint shader)
    : shader(shader)
  {}
  ~shader_deleter()
  {
    if (shader)
      glDeleteShader(shader);
  }
  GLuint shader;
};

int createShader(const GLchar * shader, GLint size, GLenum shader_type)
{
  GLuint s = glCreateShader(shader_type);
  glShaderSource(s, 1, &shader, &size);
  glCompileShader(s);

  GLint status;
  glGetShaderiv(s, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE)
  {
    GLint logSize = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &logSize);
    std::vector<GLchar> errorLog(logSize);
    glGetShaderInfoLog(s, logSize, &logSize, errorLog.data());
    fmt::print(stderr, "Failed to create shader:\n{}.\n", (const char*)errorLog.data());
    glDeleteShader(s);
    return 0;
  }
  return s;
}

int createProgram(const char *vertex_shader, size_t vertex_size, const char *fragment_shader, size_t fragment_size)
{
  GLuint vs = createShader(vertex_shader, GLint(vertex_size), GL_VERTEX_SHADER);
  shader_deleter delete_vs(vs);

  GLuint fs = createShader(fragment_shader, GLint(fragment_size), GL_FRAGMENT_SHADER);
  shader_deleter delete_fs(fs);

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);

  glLinkProgram(program);

  GLint isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
  if (isLinked == GL_FALSE)
  {
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character
    std::vector<GLchar> infoLog(maxLength);
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

    // We don't need the program anymore.
    glDeleteProgram(program);
    fmt::print(stderr, "Failed to link program:\n{}\n", infoLog.data());
    return 0 ;
  }

  glDetachShader(program, vs);
  glDetachShader(program, fs);

  return program;
}

int main(int , char** )
{
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  static const int width = 800;
  static const int height = 600;

  SDL_Window *window =
    SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                     SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_GLContext context = SDL_GL_CreateContext(window);

  if (!gladLoadGL())
  {
    fmt::print(stderr, "Failed to load opengl.");
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsClassic();

  // Setup Platform/Renderer bindings

  const char *glsl_version = "#version 130";
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

  std::string file = "test.las";
  auto *renderer = points::render::renderer_create(file.c_str(), int(file.size()));
  auto* camera = points::render::camera_create();
  auto aabb = points::render::renderer_aabb(renderer);

  glm::dvec3 aabb_center(aabb.min[0] + (aabb.max[0] - aabb.min[0]) / 2, aabb.min[1] + (aabb.max[1] - aabb.min[1]) / 2,
                         aabb.min[2] + (aabb.max[2] - aabb.min[2]) / 2);
  glm::dvec3 some_offset(-14.0, -13.0, -12.0);
  auto diff = some_offset - aabb_center;
  glm::dvec3 up(0.0, 1.0, 0.0);

//  glm::dvec3 origin(0.0, 0.0, 0.0);
//  glm::dvec3 direction(origin - aabb_center);
//  points::render::camera_look_at_aabb(camera, &aabb, glm::value_ptr(direction), glm::value_ptr(up)); 
  points::render::camera_set_perspective(camera, 45, width, height, 0.001, 1000);
  points::render::camera_look_at_aabb(camera, &aabb, glm::value_ptr(diff), glm::value_ptr(up)); 

  auto shaderfs = cmrc::shaders::get_filesystem();
  auto vertex_shader = shaderfs.open("shaders/simple.vert");
  auto fragment_shader = shaderfs.open("shaders/simple.frag");

  GLuint program = createProgram(vertex_shader.begin(), vertex_shader.end() - vertex_shader.begin(),
    fragment_shader.begin(), vertex_shader.end() - vertex_shader.begin());

  GLint attrib_position = glGetAttribLocation(program, "position");
  GLint attrib_color = glGetAttribLocation(program, "color");

  glUseProgram(program);

  glClearColor(0.5, 0.0, 0.0, 0.0);
  glViewport(0, 0, width, height);

  GLuint vao, vbo, color_buffer, ibo;

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);


  glGenBuffers(1, &vbo);
  glGenBuffers(1, &color_buffer);
  
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  
  std::vector<glm::vec3> coordinates;
  coordinates.resize(8);
  coordinates[0] = glm::vec3(aabb.min[0], aabb.min[1], aabb.min[2]);
  coordinates[1] = glm::vec3(aabb.min[0], aabb.min[1], aabb.max[2]);
  coordinates[2] = glm::vec3(aabb.min[0], aabb.max[1], aabb.min[2]);
  coordinates[3] = glm::vec3(aabb.min[0], aabb.max[1], aabb.max[2]);
  coordinates[4] = glm::vec3(aabb.max[0], aabb.min[1], aabb.min[2]);
  coordinates[5] = glm::vec3(aabb.max[0], aabb.min[1], aabb.max[2]);
  coordinates[6] = glm::vec3(aabb.max[0], aabb.max[1], aabb.min[2]);
  coordinates[7] = glm::vec3(aabb.max[0], aabb.max[1], aabb.max[2]);
  glBufferData(GL_ARRAY_BUFFER, coordinates.size() * sizeof(coordinates[0]), coordinates.data(), GL_STATIC_DRAW);
  
  glEnableVertexAttribArray(attrib_position);
  glVertexAttribPointer(attrib_position, 3, GL_FLOAT, GL_FALSE, 0, 0);

  glBindBuffer(GL_ARRAY_BUFFER, color_buffer);
  
  std::vector<glm::vec3> colors;
  colors.resize(8);
  colors[0] = glm::vec3(0.0, 0.0, 0.0);
  colors[1] = glm::vec3(0.0, 0.0, 1.0);
  colors[2] = glm::vec3(0.0, 1.0, 0.0);
  colors[3] = glm::vec3(0.0, 1.0, 1.0);
  colors[4] = glm::vec3(1.0, 0.0, 0.0);
  colors[5] = glm::vec3(1.0, 0.0, 1.0);
  colors[6] = glm::vec3(1.0, 1.0, 0.0);
  colors[7] = glm::vec3(1.0, 1.0, 1.0);
  glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(colors[0]), colors.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(attrib_color);
  glVertexAttribPointer(attrib_color, 3, GL_FLOAT, GL_TRUE, 0, 0);

  glGenBuffers(1, &ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

  std::vector<int> indecies;
  indecies.reserve(36);
  indecies.push_back(0); indecies.push_back(2); indecies.push_back(4); indecies.push_back(4); indecies.push_back(2); indecies.push_back(6);
  indecies.push_back(4); indecies.push_back(6); indecies.push_back(5); indecies.push_back(5); indecies.push_back(6); indecies.push_back(7);
  indecies.push_back(5); indecies.push_back(7); indecies.push_back(1); indecies.push_back(1); indecies.push_back(7); indecies.push_back(3);
  indecies.push_back(2); indecies.push_back(0); indecies.push_back(3); indecies.push_back(3); indecies.push_back(0); indecies.push_back(1);
  indecies.push_back(1); indecies.push_back(0); indecies.push_back(5); indecies.push_back(5); indecies.push_back(0); indecies.push_back(4);
  indecies.push_back(2); indecies.push_back(3); indecies.push_back(7); indecies.push_back(7); indecies.push_back(6); indecies.push_back(2);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indecies.size() * sizeof(indecies[0]), indecies.data(), GL_STATIC_DRAW);


  auto error = glGetError();
  (void)error;
  bool loop = true;
  bool left_pressed = false;
  bool left_right_pressed = false;
  auto *arcball = points::render::camera_manipulator::arcball_create(camera, glm::value_ptr(aabb_center));

  while(loop)
  {
    glClear(GL_COLOR_BUFFER_BIT);

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
        case SDL_KEYUP:
          if (event.key.keysym.sym == SDLK_ESCAPE)
            loop = false;
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = true;
          }
          else if (left_pressed && event.button.button == SDL_BUTTON_RIGHT)
          {
            left_right_pressed = true; 
          }
          fmt::print(stderr, "mousebutton pressed {}\n", event.button.button);
          break;
        case SDL_MOUSEMOTION:
          if (left_right_pressed)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = (float(event.motion.yrel) / float(height));
            float avg = (dx + dy) / 2;
            points::render::camera_manipulator::arcball_commit(arcball, 0.0f, 0.0f, avg);
          }
          else if (left_pressed)
          {
            float dx = (float(event.motion.xrel) / float(width));
            float dy = (float(event.motion.yrel) / float(height));
            points::render::camera_manipulator::arcball_commit(arcball, dx, dy, 0.0f);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (event.button.button == SDL_BUTTON_LEFT)
          {
            left_pressed = false;
            left_right_pressed = false;
          } else if (event.button.button == SDL_BUTTON_RIGHT)
          {
            left_right_pressed = false;
          }
          break;
        case SDL_WINDOWEVENT:
          switch (event.window.event)
          {
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            glViewport(0, 0, event.window.data1, event.window.data2);
            points::render::camera_set_perspective(camera, 45, event.window.data1, event.window.data2, 0.01, 1000);
            break;
          }
          break;
        }
      }
    }
    error = glGetError();
    glBindVertexArray(vao);
    glm::dmat4 projection_matrix;
    points::render::camera_get_perspective_matrix(camera, glm::value_ptr(projection_matrix));
    glm::dmat4 view_matrix;
    points::render::camera_get_view_matrix(camera, glm::value_ptr(view_matrix));

    glm::mat4 pv = projection_matrix * view_matrix;

    glUniformMatrix4fv(glGetUniformLocation(program, "pv"), 1, GL_FALSE, glm::value_ptr(pv));

    glDrawElements(GL_TRIANGLES, GLsizei(indecies.size()), GL_UNSIGNED_INT, 0);

    auto inv_v = glm::inverse(view_matrix);
    glm::vec4 pos(0.00001, 0.0001, 0.00001, 0.000001);
    auto projected = view_matrix* pos;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

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
