#include "gl_renderer.h"

#include <fmt/printf.h>

#define CMRC_NO_EXCEPTIONS 1
#include <cmrc/cmrc.hpp>

#pragma warning(push)
#pragma warning(disable : 4201)
#pragma warning(disable : 4127)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

CMRC_DECLARE(shaders);

struct shader_deleter
{
  shader_deleter(GLuint shader)
    : shader(shader)
  {
  }
  ~shader_deleter()
  {
    if (shader)
      glDeleteShader(shader);
  }
  GLuint shader;
};

GLuint cast_to_uint(void *ptr)
{
  intptr_t p = reinterpret_cast<intptr_t>(ptr);
  return GLuint(p);
}

void *cast_from_uint(GLuint data)
{
  intptr_t p(data);
  return reinterpret_cast<void *>(p);
}

int type_to_glformat(points::type_t type)
{
  switch (type)
  {
  case points::type_u8:
    return GL_UNSIGNED_BYTE;
  case points::type_u16:
    return GL_UNSIGNED_SHORT;
  case points::type_u32:
    return GL_UNSIGNED_INT;
  case points::type_r32:
    return GL_FLOAT;
  case points::type_r64:
    return GL_DOUBLE;
  default:
    return GL_INVALID_VALUE;
  }
}

int component_to_tex_format(points::components_t components)
{
  switch (components)
  {
  case points::components_1:
    return GL_RED;
  case points::components_2:
    return GL_RG;
  case points::components_3:
    return GL_RGB;
  case points::components_4:
    return GL_RGBA;
  default:
    break;
  }

  fmt::print(stderr, "initializing buffer with invalid component.");
  return GL_RGB;
}

int create_shader(const GLchar *shader, GLint size, GLenum shader_type)
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
    fmt::print(stderr, "Failed to create shader:\n{}.\n", (const char *)errorLog.data());
    glDeleteShader(s);
    return 0;
  }
  return s;
}

int create_program(const char *vertex_shader, size_t vertex_size, const char *fragment_shader, size_t fragment_size)
{
  GLuint vs = create_shader(vertex_shader, GLint(vertex_size), GL_VERTEX_SHADER);
  shader_deleter delete_vs(vs);

  GLuint fs = create_shader(fragment_shader, GLint(fragment_size), GL_FRAGMENT_SHADER);
  shader_deleter delete_fs(fs);

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);

  glLinkProgram(program);

  GLint isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, (int *)&isLinked);
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
    return 0;
  }

  glDetachShader(program, vs);
  glDetachShader(program, fs);

  return program;
}

gl_frame_handler::~gl_frame_handler()
{
}

gl_aabb_handler::gl_aabb_handler()
  : is_initialized(false)
{
}

gl_aabb_handler::~gl_aabb_handler()
{
  if (program)
    glDeleteProgram(program);
  if (vao)
    glDeleteVertexArrays(1, &vao);
}

void gl_aabb_handler::initialize()
{
  is_initialized = true;
  auto shaderfs = cmrc::shaders::get_filesystem();
  auto vertex_shader = shaderfs.open("shaders/aabb.vert");
  auto fragment_shader = shaderfs.open("shaders/aabb.frag");

  program = create_program(vertex_shader.begin(), vertex_shader.end() - vertex_shader.begin(), fragment_shader.begin(), vertex_shader.end() - vertex_shader.begin());
  glUseProgram(program);
  attrib_position = glGetAttribLocation(program, "position");
  attrib_color = glGetAttribLocation(program, "color");
  uniform_pv = glGetUniformLocation(program, "pv");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint tmp_buffer;
  glGenBuffers(1, &tmp_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, tmp_buffer);
  glEnableVertexAttribArray(attrib_position);
  glEnableVertexAttribArray(attrib_color);
  glBindVertexArray(0);
  glDeleteBuffers(1, &tmp_buffer);
  glBindVertexArray(0);
}

void gl_aabb_handler::draw(points::render::draw_group_t &group)
{
  if (!is_initialized)
    initialize();
  glUseProgram(program);

  glBindVertexArray(vao);
  glUseProgram(program);
  int index_buffer_type = GL_INVALID_VALUE;
  for (int i = 0; i < group.buffers_size; i++)
  {
    auto &buffer = group.buffers[i];
    points::render::aabb_mesh_buffer_mapping_t buffer_mapping = points::render::aabb_mesh_buffer_mapping_t(buffer.buffer_mapping);
    switch (buffer_mapping)
    {
    case points::render::aabb_bm_color: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(attrib_color, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_TRUE, 0, 0);
      break;
    }
    case points::render::aabb_bm_position: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(attrib_position, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_FALSE, 0, 0);
      break;
    }
    case points::render::aabb_bm_index: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_buffer->id);
      index_buffer_type = type_to_glformat(gl_buffer->type);
      break;
    }
    case points::render::aabb_bm_camera: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glUniformMatrix4fv(uniform_pv, 1, GL_FALSE, (const GLfloat *)gl_buffer->data);
      break;
    }
    }
  }

  glDrawElements(GL_TRIANGLES, group.draw_size, index_buffer_type, 0);
  glBindVertexArray(0);
}

gl_dyn_points_handler::gl_dyn_points_handler()
  : vao(0)
  , program(0)
  , is_initialized(false)
{
}

gl_dyn_points_handler::~gl_dyn_points_handler()
{
  if (program)
    glDeleteProgram(program);
  if (vao)
    glDeleteVertexArrays(1, &vao);
}

void gl_dyn_points_handler::initialize()
{
  is_initialized = true;
  auto shaderfs = cmrc::shaders::get_filesystem();
  auto vertex_shader = shaderfs.open("shaders/dynpoints.vert");
  auto fragment_shader = shaderfs.open("shaders/dynpoints.frag");

  program = create_program(vertex_shader.begin(), vertex_shader.end() - vertex_shader.begin(), fragment_shader.begin(), vertex_shader.end() - vertex_shader.begin());
  glUseProgram(program);
  rgb_position = glGetAttribLocation(program, "rgb");
  vertex_position = glGetAttribLocation(program, "position");
  uniform_camera = glGetUniformLocation(program, "camera");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint tmp_buffer;
  glGenBuffers(1, &tmp_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, tmp_buffer);
  glEnableVertexAttribArray(vertex_position);
  glEnableVertexAttribArray(rgb_position);
  glBindVertexArray(0);
  glDeleteBuffers(1, &tmp_buffer);
  glBindVertexArray(0);
}

void gl_dyn_points_handler::draw(points::render::draw_group_t &group)
{
  if (!is_initialized)
    initialize();
  glUseProgram(program);

  glPointSize(2);

  glBindVertexArray(vao);
  glUseProgram(program);
  for (int i = 0; i < group.buffers_size; i++)
  {
    auto &buffer = group.buffers[i];
    auto buffer_mapping = points::render::dyn_points_buffer_mapping_t(buffer.buffer_mapping);
    switch (buffer_mapping)
    {
    case points::render::dyn_points_bm_vertex: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(vertex_position, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_FALSE, 0, 0);
      break;
    }
    case points::render::dyn_points_bm_color: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(rgb_position, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_TRUE, 0, 0);
      break;
    }
    case points::render::dyn_points_bm_camera: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glUniformMatrix4fv(uniform_camera, 1, GL_FALSE, (const GLfloat *)gl_buffer->data);
      break;
    }
    }
  }

  glDrawArrays(GL_POINTS, 0, group.draw_size);
  glBindVertexArray(0);
}

gl_flat_points_handler::gl_flat_points_handler()
  : vao(0)
  , program(0)
  , is_initialized(false)
{
}

gl_flat_points_handler::~gl_flat_points_handler()
{
  if (program)
    glDeleteProgram(program);
  if (vao)
    glDeleteVertexArrays(1, &vao);
}

void gl_flat_points_handler::initialize()
{
  is_initialized = true;
  auto shaderfs = cmrc::shaders::get_filesystem();
  auto vertex_shader = shaderfs.open("shaders/points.vert");
  auto fragment_shader = shaderfs.open("shaders/points.frag");

  program = create_program(vertex_shader.begin(), vertex_shader.end() - vertex_shader.begin(), fragment_shader.begin(), vertex_shader.end() - vertex_shader.begin());
  glUseProgram(program);
  attrib_position = glGetAttribLocation(program, "position");
  attrib_color = glGetAttribLocation(program, "color");
  uniform_camera = glGetUniformLocation(program, "camera");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint tmp_buffer;
  glGenBuffers(1, &tmp_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, tmp_buffer);
  glEnableVertexAttribArray(attrib_position);
  glEnableVertexAttribArray(attrib_color);
  glBindVertexArray(0);
  glDeleteBuffers(1, &tmp_buffer);
  glBindVertexArray(0);
}

void gl_flat_points_handler::draw(points::render::draw_group_t &group)
{
  if (!is_initialized)
    initialize();
  glUseProgram(program);

  glBindVertexArray(vao);
  glUseProgram(program);

  for (int i = 0; i < group.buffers_size; i++)
  {
    auto &buffer = group.buffers[i];
    auto buffer_mapping = points::render::points_buffer_mapping_t(buffer.buffer_mapping);
    switch (buffer_mapping)
    {
    case points::render::points_bm_color: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(attrib_color, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_TRUE, 0, 0);
      break;
    }
    case points::render::points_bm_vertex: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(attrib_position, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_FALSE, 0, 0);
      break;
    }
    case points::render::points_bm_camera: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glUniformMatrix4fv(uniform_camera, 1, GL_FALSE, (const GLfloat *)gl_buffer->data);
      break;
    }
    }
  }

  glDrawArrays(GL_POINTS, 0, group.draw_size);
  glBindVertexArray(0);
}

gl_skybox_handler::gl_skybox_handler()
  : is_initialized(false)
{
}

gl_skybox_handler::~gl_skybox_handler()
{
  if (program)
    glDeleteProgram(program);
  if (vao)
    glDeleteVertexArrays(1, &vao);
}

void gl_skybox_handler::initialize()
{
  is_initialized = true;
  auto shaderfs = cmrc::shaders::get_filesystem();
  auto vertex_shader = shaderfs.open("shaders/skybox.vert");
  auto fragment_shader = shaderfs.open("shaders/skybox.frag");

  program = create_program(vertex_shader.begin(), vertex_shader.end() - vertex_shader.begin(), fragment_shader.begin(), vertex_shader.end() - vertex_shader.begin());
  glUseProgram(program);
  attrib_vertex = glGetAttribLocation(program, "vertex");
  uniform_inverse_vp = glGetUniformLocation(program, "inverse_vp");
  uniform_skybox = glGetUniformLocation(program, "skybox");
  uniform_camera_pos = glGetUniformLocation(program, "camera_pos");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint tmp_buffer;
  glGenBuffers(1, &tmp_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, tmp_buffer);
  glEnableVertexAttribArray(attrib_vertex);
  glBindVertexArray(0);
  glDeleteBuffers(1, &tmp_buffer);
  glBindVertexArray(0);
}

void gl_skybox_handler::draw(points::render::draw_group_t &group)
{
  if (!is_initialized)
    initialize();
  glUseProgram(program);
  glBindVertexArray(vao);
  glUseProgram(program);
  for (int i = 0; i < group.buffers_size; i++)
  {
    auto &buffer = group.buffers[i];
    auto buffer_mapping = points::render::skybox_buffer_mapping_t(buffer.buffer_mapping);
    switch (buffer_mapping)
    {
    case points::render::skybox_bm_vertex: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glBindBuffer(GL_ARRAY_BUFFER, gl_buffer->id);
      glVertexAttribPointer(attrib_vertex, gl_buffer->components, type_to_glformat(gl_buffer->type), GL_FALSE, 0, 0);
      break;
    }
    case points::render::skybox_bm_inverse_view_projection: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glUniformMatrix4fv(uniform_inverse_vp, 1, GL_FALSE, (const GLfloat *)gl_buffer->data);
      break;
    }
    case points::render::skybox_bm_camera_pos: {
      gl_buffer_t *gl_buffer = static_cast<gl_buffer_t *>(buffer.user_ptr);
      glUniform3fv(uniform_camera_pos, 1, (const GLfloat *)gl_buffer->data);
      break;
    }
    case points::render::skybox_bm_cube_map_texture: {
      gl_texture_t *gl_texture = static_cast<gl_texture_t *>(buffer.user_ptr);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_CUBE_MAP, gl_texture->id);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
      break;
    }
    }
  }
  glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindVertexArray(0);
}

gl_renderer::gl_renderer(points::render::renderer_t *renderer, points::render::camera_t *camera)
  : renderer(renderer)
  , camera(camera)
{
  points::render::renderer_callbacks_t callbacks = {};
  callbacks.dirty = &static_dirty_callback;
  callbacks.create_buffer = &static_create_buffer;
  callbacks.initialize_buffer = &static_initialize_buffer;
  callbacks.modify_buffer = &static_modify_buffer;
  callbacks.destroy_buffer = &static_destroy_buffer;
  callbacks.create_texture = &static_create_texture;
  callbacks.initialize_texture = &static_initialize_texture;
  callbacks.modify_texture = &static_modify_texture;
  callbacks.destroy_texture = &static_destroy_texture;
  points::render::renderer_set_callback(renderer, callbacks, this);
}

static void create_index_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &index_buffers)
{
  index_buffers.emplace_back(buffer);
  glGenBuffers(1, &buffer->id);
}

void create_vertex_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &vertex_buffers)
{
  vertex_buffers.emplace_back(buffer);
  glGenBuffers(1, &buffer->id);
}

void create_uniform_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &uniform_buffers)
{
  uniform_buffers.emplace_back(buffer);
}

static void initialize_index_buffer(gl_buffer_t *buffer, int data_size, void *data)
{
  assert(data_size > 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->id);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, data_size, data, GL_STATIC_DRAW);
  points::render::buffer_release_data(buffer->buffer);
}

static void initialize_vertex_buffer(gl_buffer_t *buffer, int data_size, void *data)
{
  assert(data_size > 0);
  glBindBuffer(GL_ARRAY_BUFFER, buffer->id);
  glBufferData(GL_ARRAY_BUFFER, data_size, data, GL_STATIC_DRAW);
  points::render::buffer_release_data(buffer->buffer);
}

static void initialize_uniform_buffer(gl_buffer_t *buffer, int data_size, void *data)
{
  buffer->data = data;
  buffer->data_size = data_size;
  buffer->data_needs_upload = true;
}
void update_index_buffer(gl_buffer_t *buffer, int offset, int data_size, void *data)
{
  if (data_size)
  {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->id);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, data_size, data);
    points::render::buffer_release_data(buffer->buffer);
  }
}

void update_vertex_buffer(gl_buffer_t *buffer, int offset, int data_size, void *data)
{
  if (data_size)
  {
    glBindBuffer(GL_ARRAY_BUFFER, buffer->id);
    glBufferSubData(GL_ARRAY_BUFFER, offset, data_size, data);
    points::render::buffer_release_data(buffer->buffer);
  }
}

void update_uniform_buffer(gl_buffer_t *buffer, int offset, int data_size, void *data)
{
  if (buffer->data_is_update)
  {
    fmt::print(stderr, "Detecting multiple buffer update within the same frame! Illigal state\n");
    return;
  }
  if (data_size)
  {
    if (buffer->data_needs_upload)
    {
      memcpy((uint8_t *)buffer->data + offset, data, data_size);
    }
    else
    {
      buffer->data = data;
      buffer->data_size = data_size;
    }
  }
}

void destroy_index_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &index_buffers)
{
  auto it = std::find(index_buffers.begin(), index_buffers.end(), buffer);
  if (it == index_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete ibo {} but its not registed in index list.\n", buffer->id);
    return;
  }

  index_buffers.erase(it);
  glDeleteBuffers(1, &buffer->id);
}

void destroy_vertex_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &vertex_buffers)
{
  auto it = std::find(vertex_buffers.begin(), vertex_buffers.end(), buffer);
  if (it == vertex_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete vbo {} but its not registed in vertex list.\n", buffer->id);
    return;
  }

  vertex_buffers.erase(it);
  glDeleteBuffers(1, &buffer->id);
}

void destroy_uniform_buffer(gl_buffer_t *buffer, std::vector<gl_buffer_t *> &uniform_buffers)
{
  auto it = std::find(uniform_buffers.begin(), uniform_buffers.end(), buffer);
  if (it == uniform_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete vbo {} but its not registed in uniform list.\n", buffer->id);
    return;
  }
  uniform_buffers.erase(it);
}

void gl_renderer::draw(clear clear, int viewport_width, int viewport_height)
{
  glEnable(GL_DEPTH_TEST);

  glClearColor(0.5, 0.5, 0.0, 1.0);

  GLbitfield clear_mask = 0;
  if (int(clear) & int(clear::depth))
    clear_mask |= GL_DEPTH_BUFFER_BIT;
  if (int(clear) & int(clear::color))
    clear_mask |= GL_COLOR_BUFFER_BIT;
  if (clear_mask)
    glClear(clear_mask);

  glViewport(0, 0, viewport_width, viewport_height);

  auto frame = points::render::renderer_frame(renderer, camera);
  for (int i = 0; i < frame.to_render_size; i++)
  {
    auto &to_render = frame.to_render[i];
    switch (to_render.draw_type)
    {
    case points::render::aabb_triangle_mesh:
      aabb_handler.draw(to_render);
      break;
    case points::render::skybox_triangle:
      skybox_handler.draw(to_render);
      break;
    case points::render::flat_points:
      points_handler.draw(to_render);
      break;
    case points::render::dyn_points:
      dynpoints_handler.draw(to_render);
      break;
    default:
      fmt::print(stderr, "Missing gl handler!.");
      abort();
    }
  }
}

void gl_renderer::static_dirty_callback(struct points::render::renderer_t *renderer, void *renderer_user_ptr)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->dirty_callback();
}
void gl_renderer::static_create_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, enum points::render::buffer_type_t buffer_type, void **buffer_user_ptr)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->create_buffer(buffer_type, buffer_user_ptr);
}
void gl_renderer::static_initialize_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *buffer_user_ptr, enum points::type_t type,
                                           enum points::components_t components, int buffer_size, void *data)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->initialize_buffer(buffer, buffer_user_ptr, type, components, buffer_size, data);
}
void gl_renderer::static_modify_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->modify_buffer(buffer, buffer_user_ptr, offset, buffer_size, data);
}
void gl_renderer::static_destroy_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, void *buffer_user_ptr)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->destroy_buffer(buffer_user_ptr);
}
void gl_renderer::static_create_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, enum points::render::texture_type_t buffer_texture_type, void **buffer_user_ptr)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->create_texture(buffer_texture_type, buffer_user_ptr);
}
void gl_renderer::static_initialize_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *texture_user_ptr,
                                            enum points::render::texture_type_t buffer_texture_type, enum points::type_t type, enum points::components_t components, int size[3], void *data)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->initialize_texture(buffer, texture_user_ptr, buffer_texture_type, type, components, size, data);
}
void gl_renderer::static_modify_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *texture_user_ptr,
                                        enum points::render::texture_type_t buffer_texture_type, int offset[3], int size[3], void *data)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->modify_texture(buffer, texture_user_ptr, buffer_texture_type, offset, size, data);
}
void gl_renderer::static_destroy_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, void *texture_user_ptr)
{
  (void)renderer;
  static_cast<gl_renderer *>(renderer_user_ptr)->destroy_texture(texture_user_ptr);
}

void gl_renderer::dirty_callback()
{
}

void gl_renderer::create_buffer(enum points::render::buffer_type_t buffer_type, void **buffer_user_ptr)
{
  gl_buffer_t *buffer = new gl_buffer_t;
  buffer->buffer_type = buffer_type;
  *buffer_user_ptr = buffer;
  switch (buffer_type)
  {
  case points::render::buffer_type_index:
    create_index_buffer(buffer, index_buffers);
    break;
  case points::render::buffer_type_vertex:
    create_vertex_buffer(buffer, vertex_buffers);
    break;
  case points::render::buffer_type_uniform:
    create_uniform_buffer(buffer, uniform_buffers);
    break;
  default:
    fmt::print(stderr, "unexpected create_buffer\n");
  }
}

void gl_renderer::initialize_buffer(struct points::render::buffer_t *buffer, void *buffer_user_ptr, enum points::type_t type, enum points::components_t components, int data_size, void *data)
{
  auto gl_buffer = static_cast<gl_buffer_t *>(buffer_user_ptr);
  gl_buffer->buffer = buffer;
  gl_buffer->type = type;
  gl_buffer->components = components;
  switch (gl_buffer->buffer_type)
  {
  case points::render::buffer_type_index:
    initialize_index_buffer(gl_buffer, data_size, data);
    break;
  case points::render::buffer_type_vertex:
    initialize_vertex_buffer(gl_buffer, data_size, data);
    break;
  case points::render::buffer_type_uniform:
    initialize_uniform_buffer(gl_buffer, data_size, data);
    break;
  default:
    fmt::print(stderr, "Unexpected buffer_type_t in initialize buffer\n.");
  }
}

void gl_renderer::modify_buffer(struct points::render::buffer_t *buffer, void *buffer_user_ptr, int offset, int data_size, void *data)
{
  (void)buffer;
  auto *gl_buffer = static_cast<gl_buffer_t *>(buffer_user_ptr);

  switch (gl_buffer->buffer_type)
  {
  case points::render::buffer_type_index:
    update_index_buffer(gl_buffer, offset, data_size, data);
    break;
  case points::render::buffer_type_vertex:
    update_vertex_buffer(gl_buffer, offset, data_size, data);
    break;
  case points::render::buffer_type_uniform:
    update_uniform_buffer(gl_buffer, offset, data_size, data);
    break;
  default:
    fmt::print(stderr, "Modify buffer callback, unsupported buffer_type\n");
  }
}

void gl_renderer::destroy_buffer(void *buffer_user_ptr)
{
  auto *gl_buffer = static_cast<gl_buffer_t *>(buffer_user_ptr);
  switch (gl_buffer->buffer_type)
  {
  case points::render::buffer_type_index:
    destroy_index_buffer(gl_buffer, index_buffers);
    break;
  case points::render::buffer_type_vertex:
    destroy_vertex_buffer(gl_buffer, vertex_buffers);
    break;
  case points::render::buffer_type_uniform:
    destroy_uniform_buffer(gl_buffer, uniform_buffers);
    break;
  default:
    fmt::print(stderr, "Modify buffer callback, unsupported buffer_type\n");
  }
  delete gl_buffer;
}
void gl_renderer::create_texture(enum points::render::texture_type_t buffer_texture_type, void **buffer_user_ptr)
{
  auto gl_texture = new gl_texture_t();
  gl_texture->texture_type = buffer_texture_type;
  glGenTextures(1, &gl_texture->id);
  *buffer_user_ptr = gl_texture;
}

void gl_renderer::initialize_texture(struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, enum points::type_t type,
                                     enum points::components_t components, int size[3], void *data)
{
  auto gl_texture = static_cast<gl_texture_t *>(texture_user_ptr);
  gl_texture->buffer = buffer;
  gl_texture->type = type;
  gl_texture->components = components;
  memcpy(&gl_texture->size, size, sizeof(gl_texture->size));
  GLenum tex_format = component_to_tex_format(components);
  GLenum data_format = type_to_glformat(type);
  GLenum bind_target;
  GLenum image_target;
  if (gl_texture->texture_type == points::render::texture_type_2d)
  {
    if (buffer_texture_type != points::render::texture_type_2d)
    {
      fmt::print(stderr, "Illigal state, initializing a 2d texture with non 2d texture data\n.");
      return;
    }
    bind_target = GL_TEXTURE_2D;
    image_target = GL_TEXTURE_2D;
  }
  else if (buffer_texture_type == points::render::texture_type_cubemap)
  {
    fmt::print(stderr, "Illigal state, initializing a cubemap with cubemap: must specify buffer_texture_type_t that "
                       "specifies direction.\n");
    return;
  }
  else
  {
    bind_target = GL_TEXTURE_CUBE_MAP;
    int diff = int(buffer_texture_type) - int(points::render::texture_type_cubemap_positive_x);
    image_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + diff;
  }

  glBindTexture(bind_target, gl_texture->id);
  (void)data;
  (void)data_format;
  (void)tex_format;
  glTexImage2D(image_target, 0, GL_RGB, size[0], size[1], 0, tex_format, data_format, data);
  points::render::buffer_release_data(buffer);
}

void gl_renderer::modify_texture(struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, int offset[3], int size[3], void *data)
{
  auto gl_texture = static_cast<gl_texture_t *>(texture_user_ptr);
  GLenum tex_format = component_to_tex_format(gl_texture->components);
  GLenum data_format = type_to_glformat(gl_texture->type);
  GLenum bind_target;
  GLenum image_target;

  if (gl_texture->texture_type == points::render::texture_type_2d)
  {
    if (buffer_texture_type != points::render::texture_type_2d)
    {
      fmt::print(stderr, "Illigal state, modifying a 2d texture with non 2d texture data.\n");
      return;
    }
    bind_target = GL_TEXTURE_2D;
    image_target = GL_TEXTURE_2D;
  }
  else if (buffer_texture_type == points::render::texture_type_cubemap)
  {
    fmt::print(stderr, "Illigal state, initializing a cubemap with cubemap: must specify buffer_texture_type_t that "
                       "specifies direction.\n");
    return;
  }
  else
  {
    bind_target = GL_TEXTURE_CUBE_MAP;
    int diff = int(buffer_texture_type) - int(points::render::texture_type_cubemap_positive_x);
    image_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + diff;
  }

  glBindTexture(bind_target, gl_texture->id);
  glTexSubImage2D(image_target, 0, offset[0], offset[1], size[0], size[1], tex_format, data_format, data);
  points::render::buffer_release_data(buffer);
}

void gl_renderer::destroy_texture(void *texture_user_ptr)
{
  gl_texture_t *texture = static_cast<gl_texture_t *>(texture_user_ptr);
  auto it = std::find(texture_buffers.begin(), texture_buffers.end(), texture);
  if (it == texture_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete texture {} but its not registed in texture list.\n", texture->id);
    return;
  }

  texture_buffers.erase(it);
  glDeleteTextures(1, &texture->id);
  delete texture;
}
