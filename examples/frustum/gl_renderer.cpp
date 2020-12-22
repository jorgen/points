#include "gl_renderer.h"

#include <fmt/printf.h>

#include <cmrc/cmrc.hpp>

#pragma warning(push)
#pragma warning(disable : 4201 )
#pragma warning(disable : 4127 )  
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

CMRC_DECLARE(shaders);
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

int format_to_glformat(points::render::buffer_format format)
{
  switch (format)
  {
  case points::render::u8: return GL_UNSIGNED_BYTE;
  case points::render::u16: return GL_UNSIGNED_SHORT;
  case points::render::u32: return GL_UNSIGNED_INT;
  case points::render::r32: return GL_FLOAT;
  case points::render::r64: return GL_DOUBLE;
  default: return GL_INVALID_VALUE;
  }
}

GLboolean normalize_to_glnormalize(points::render::buffer_data_normalize normalize)
{
  switch (normalize)
  {
  case points::render::normalize:
    return GL_TRUE; 
  default:
    return GL_FALSE;
  }
}

int create_shader(const GLchar * shader, GLint size, GLenum shader_type)
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
  auto vertex_shader = shaderfs.open("shaders/simple.vert");
  auto fragment_shader = shaderfs.open("shaders/simple.frag");

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
}

void gl_aabb_handler::set_camera_state(points::render::camera *camera)
{
  if (!is_initialized)
    initialize();
  glUseProgram(program);

  glm::dmat4 projection_matrix;
  points::render::camera_get_perspective_matrix(camera, glm::value_ptr(projection_matrix));
  glm::dmat4 view_matrix;
  points::render::camera_get_view_matrix(camera, glm::value_ptr(view_matrix));

  glm::mat4 pv = projection_matrix * view_matrix;

  glUniformMatrix4fv(uniform_pv, 1, GL_FALSE, glm::value_ptr(pv));
}

void gl_aabb_handler::draw(points::render::draw_group &group)
{
  glBindVertexArray(vao);
  glUseProgram(program);
  int index_buffer_type = GL_INVALID_VALUE;
  for (int i = 0; i < group.buffers_size; i++)
  {
    auto &buffer = group.buffers[i];
    GLuint buffer_id = cast_to_uint(*buffer.user_ptr);
    if (buffer.type == points::render::buffer_type_vertex)
    {
      switch (points::render::aabb_triangle_mesh_buffer_mapping(buffer.buffer_mapping))
      {
      case points::render::aabb_triangle_mesh_color:
        glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        glVertexAttribPointer(attrib_color, buffer.components, format_to_glformat(buffer.format), normalize_to_glnormalize(buffer.normalize), 0, 0);
        break;
      case points::render::aabb_triangle_mesh_position:
        glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        glVertexAttribPointer(attrib_position, buffer.components, format_to_glformat(buffer.format), normalize_to_glnormalize(buffer.normalize), 0, 0);
      }
    }
    else if (buffer.type == points::render::buffer_type_index)
    {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_id);
      index_buffer_type = format_to_glformat(buffer.format);
    }
    else
    {
      fmt::print(stderr, "Invalid buffer type {} for gl_aabb_handler\n", buffer.type); 
    }
  }

  glDrawElements(GL_TRIANGLES, group.draw_size, index_buffer_type, 0);

}

gl_renderer::gl_renderer(points::render::renderer *renderer, points::render::camera *camera)
  : renderer(renderer)
  , camera(camera)
{

}

void create_index_buffer(points::render::buffer &buffer, std::vector<GLuint> &index_buffers)
{
  GLuint ibo;
  glGenBuffers(1, &ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  auto size = points::render::buffer_data_size(buffer.data);
  assert(size > 0);
  const void *data = points::render::buffer_data_get(buffer.data);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
  *buffer.user_ptr = cast_from_uint(ibo);
  index_buffers.emplace_back(ibo);
  points::render::buffer_data_set_rendered(buffer.data);
}

void create_vertex_buffer(points::render::buffer &buffer, std::vector<GLuint> &vertex_buffers)
{
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  auto size = points::render::buffer_data_size(buffer.data);
  assert(size > 0);
  const void *data = points::render::buffer_data_get(buffer.data);
  glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
  *buffer.user_ptr = cast_from_uint(vbo);
  vertex_buffers.emplace_back(vbo);
  points::render::buffer_data_set_rendered(buffer.data);
}

void update_index_buffer(points::render::buffer &buffer, std::vector<GLuint> &index_buffers)
{
  (void)index_buffers;
  GLuint ibo;
  ibo = cast_to_uint(*buffer.user_ptr);
  auto size = points::render::buffer_data_size(buffer.data);
  auto offset = points::render::buffer_data_offset(buffer.data);
  auto data = points::render::buffer_data_get(buffer.data);
  if (size)
  {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, size, data);
  }
}

void update_vertex_buffer(points::render::buffer &buffer, std::vector<GLuint> &vertex_buffers)
{
  (void)vertex_buffers;
  GLuint vbo;
  vbo = cast_to_uint(*buffer.user_ptr);
  auto size = points::render::buffer_data_size(buffer.data);
  auto offset = points::render::buffer_data_offset(buffer.data);
  auto data = points::render::buffer_data_get(buffer.data);
  if (size)
  {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
  }
}

void remove_index_buffer(points::render::buffer &buffer, std::vector<GLuint> &index_buffers)
{
  GLuint ibo;
  ibo = cast_to_uint(*buffer.user_ptr);
  auto it = std::find(index_buffers.begin(), index_buffers.end(), ibo);
  if (it == index_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete ibo {} but its not registed in index list.\n", ibo);
    return;
  }

  index_buffers.erase(it);
  glDeleteBuffers(1, &ibo);
}

void remove_vertex_buffer(points::render::buffer &buffer, std::vector<GLuint> &vertex_buffers)
{
  GLuint vbo;
  vbo = cast_to_uint(*buffer.user_ptr);
  auto it = std::find(vertex_buffers.begin(), vertex_buffers.end(), vbo);
  if (it == vertex_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete vbo {} but its not registed in vertex list.\n", vbo);
    return;
  }

  vertex_buffers.erase(it);
  glDeleteBuffers(1, &vbo);
}

void gl_renderer::draw(clear clear, int viewport_width, int viewport_height)
{
  glEnable(GL_DEPTH_TEST);

  glClearColor(0.5, 0.0, 0.0, 0.0);

  GLbitfield clear_mask = 0;
  if (int(clear) & int(clear::depth))
    clear_mask |= GL_DEPTH_BUFFER_BIT;
  if (int(clear) & int(clear::color))
    clear_mask |= GL_COLOR_BUFFER_BIT;
  if (clear_mask)
    glClear(clear_mask);

  glViewport(0, 0, viewport_width, viewport_height);

  auto frame = points::render::renderer_frame(renderer, camera);
  for (int i = 0; i < frame.to_add_size; i++)
  {
    auto &to_add = frame.to_add[i];
    switch (to_add.type)
    {
    case points::render::buffer_type_index:
      create_index_buffer(to_add, index_buffers);
      break;
    case points::render::buffer_type_vertex:
      create_vertex_buffer(to_add, vertex_buffers);
      break;
    }
  }
  for (int i = 0; i < frame.to_update_size; i++)
  {
    auto &to_update = frame.to_update[i];
    switch (to_update.type)
    {
    case points::render::buffer_type_index:
      update_index_buffer(to_update, index_buffers);
      break;
    case points::render::buffer_type_vertex:
      update_vertex_buffer(to_update, vertex_buffers);
      break;
    }
  }
  for (int i = 0; i < frame.to_remove_size; i++)
  {
    auto &to_remove = frame.to_remove[i];
    switch (to_remove.type)
    {
    case points::render::buffer_type_index:
      remove_index_buffer(to_remove, index_buffers);
      break;
    case points::render::buffer_type_vertex:
      remove_vertex_buffer(to_remove, vertex_buffers);
      break;
    }
  }

  for (int i = 0; i < frame.to_render_size; i++)
  {
    auto &to_render = frame.to_render[i];
    switch (to_render.draw_type)
    {
    case points::render::aabb_triangle_mesh:
      aabb_handler.set_camera_state(camera);
      aabb_handler.draw(to_render);
    }
  }
}
