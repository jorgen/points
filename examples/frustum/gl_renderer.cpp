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

int format_to_glformat(points::render::buffer_format_t format)
{
  switch (format)
  {
  case points::render::buffer_format_u8: return GL_UNSIGNED_BYTE;
  case points::render::buffer_format_u16: return GL_UNSIGNED_SHORT;
  case points::render::buffer_format_u32: return GL_UNSIGNED_INT;
  case points::render::buffer_format_r32: return GL_FLOAT;
  case points::render::buffer_format_r64: return GL_DOUBLE;
  default: return GL_INVALID_VALUE;
  }
}

int component_to_tex_format(points::render::buffer_components_t components)
{
  switch (components)
  {
  case points::render::buffer_components_1: return GL_RED;
  case points::render::buffer_components_2: return GL_RG;
  case points::render::buffer_components_3: return GL_RGB;
  case points::render::buffer_components_4: return GL_RGBA;
  default:
    break;
  }
    
  fmt::print(stderr, "initializing buffer with invalid component.");
  return GL_RGB;
}

GLboolean normalize_to_glnormalize(points::render::buffer_normalize_t normalize)
{
  switch (normalize)
  {
  case points::render::buffer_normalize_normalize:
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
    GLuint buffer_id = cast_to_uint(points::render::buffer_user_ptr(buffer.data));
    auto buffer_type = points::render::buffer_type(buffer.data);
    auto buffer_mapping = points::render::aabb_triangle_mesh_buffer_mapping_t(points::render::buffer_mapping(buffer.data));
    auto buffer_components = points::render::buffer_components(buffer.data);
    auto buffer_format = points::render::buffer_format(buffer.data);
    auto buffer_normalize = points::render::buffer_normalize(buffer.data);
    if (buffer_type == points::render::buffer_type_vertex)
    {
      switch (buffer_mapping)
      {
      case points::render::aabb_triangle_mesh_color:
        glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        glVertexAttribPointer(attrib_color, buffer_components, format_to_glformat(buffer_format), normalize_to_glnormalize(buffer_normalize), 0, 0);
        break;
      case points::render::aabb_triangle_mesh_position:
        glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        glVertexAttribPointer(attrib_position, buffer_components, format_to_glformat(buffer_format), normalize_to_glnormalize(buffer_normalize), 0, 0);
      }
    }
    else if (buffer_type == points::render::buffer_type_index)
    {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_id);
      index_buffer_type = format_to_glformat(buffer_format);
    }
    else if (buffer_type == points::render::buffer_type_uniform)
    {
      if (buffer_mapping == points::render::aabb_triangle_mesh_camera)
        glUniformMatrix4fv(uniform_pv, 1, GL_FALSE, (const GLfloat *)points::render::buffer_get(buffer.data));
      else
        fmt::print(stderr, "Internal state error. Not expected buffer_type.");
    }
    else
    {
      fmt::print(stderr, "Invalid buffer type {} for gl_aabb_handler\n", buffer_type);
    }
  }

  glDrawElements(GL_TRIANGLES, group.draw_size, index_buffer_type, 0);
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
  attrib_position = glGetAttribLocation(program, "position");
  uniform_inverse_vp = glGetUniformLocation(program, "inverse_vp");
  uniform_skybox = glGetUniformLocation(program, "skybox");
  uniform_camera_pos = glGetUniformLocation(program, "camera_pos");

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint tmp_buffer;
  glGenBuffers(1, &tmp_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, tmp_buffer);
  glEnableVertexAttribArray(attrib_position);
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
    GLuint buffer_id = cast_to_uint(points::render::buffer_user_ptr(buffer.data));
    auto buffer_type = points::render::buffer_type(buffer.data);
    auto buffer_mapping = points::render::skybox_buffer_mapping_t(points::render::buffer_mapping(buffer.data));
    auto buffer_components = points::render::buffer_components(buffer.data);
    auto buffer_format = points::render::buffer_format(buffer.data);
    auto buffer_normalize = points::render::buffer_normalize(buffer.data);
    if (buffer_type == points::render::buffer_type_vertex)
    {
      switch (buffer_mapping)
      {
      case points::render::skybox_vertex:
        glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        glVertexAttribPointer(attrib_position, buffer_components, format_to_glformat(buffer_format), normalize_to_glnormalize(buffer_normalize), 0, 0);
        break;
      }
    }
    else if (buffer_type == points::render::buffer_type_uniform)
    {
      if (buffer_mapping == points::render::skybox_inverse_view_projection)
        glUniformMatrix4fv(uniform_inverse_vp, 1, GL_FALSE, (const GLfloat *)points::render::buffer_get(buffer.data));
      else if (buffer_mapping == points::render::skybox_camera_pos)
        glUniform3fv(uniform_camera_pos, 1, (const GLfloat *)points::render::buffer_get(buffer.data));
    }
    else if (buffer_type == points::render::buffer_type_texture)
    {
      if (buffer_mapping == points::render::skybox_texture_cube)
      {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, buffer_id);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);  
      }
    }
    else
    {
      fmt::print(stderr, "Invalid buffer type {} for gl_skybox_handler\n", buffer_type);
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
  callbacks.dirty = &dirty_callback_static;
  callbacks.create_buffer = &create_buffer_static;
  callbacks.initialize_buffer = &initialize_buffer_static;
  callbacks.modify_buffer = &modify_buffer_static;
  callbacks.destroy_buffer = &modify_buffer_static;
  points::render::renderer_set_callback(renderer, callbacks, this);
}

void create_index_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &index_buffers)
{
  GLuint ibo;
  glGenBuffers(1, &ibo);
  points::render::buffer_set_user_ptr(buffer, cast_from_uint(ibo));
  index_buffers.emplace_back(ibo);
}
void initialize_index_buffer(points::render::buffer_t *buffer)
{
  GLuint ibo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  auto size = points::render::buffer_size(buffer);
  assert(size > 0);
  const void *data = points::render::buffer_get(buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}

void create_vertex_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &vertex_buffers)
{
  GLuint vbo;
  glGenBuffers(1, &vbo);
  points::render::buffer_set_user_ptr(buffer, cast_from_uint(vbo));
  vertex_buffers.emplace_back(vbo);
}

void initialize_vertex_buffer(points::render::buffer_t *buffer)
{
  GLuint vbo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  auto size = points::render::buffer_size(buffer);
  assert(size > 0);
  const void *data = points::render::buffer_get(buffer);
  glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
}

void create_texure_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &texture_buffers)
{
  GLuint texture;
  glGenTextures(1, &texture);
  points::render::buffer_set_user_ptr(buffer, cast_from_uint(texture));
  texture_buffers.emplace_back(texture);
}

void initialize_texture_buffer(points::render::buffer_t *buffer)
{
  auto texture_type = points::render::buffer_texture_type(buffer);
  GLuint texture = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto width = points::render::buffer_width(buffer);
  auto height = points::render::buffer_height(buffer);
  auto data_format = format_to_glformat(points::render::buffer_format(buffer));
  GLenum tex_format = component_to_tex_format(points::render::buffer_components(buffer));
  auto data = points::render::buffer_get(buffer);
  switch (texture_type)
  {
  case points::render::buffer_texture_2d:
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, tex_format, data_format,
                 points::render::buffer_get(buffer));
    break;
  case points::render::buffer_texture_cubemap_positive_x:
  case points::render::buffer_texture_cubemap_negative_x:
  case points::render::buffer_texture_cubemap_positive_y:
  case points::render::buffer_texture_cubemap_negative_y:
  case points::render::buffer_texture_cubemap_positive_z:
  case points::render::buffer_texture_cubemap_negative_z:
    int diff = int(texture_type) - int(points::render::buffer_texture_cubemap_positive_x);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + diff, 0, GL_RGB, width, height, 0, tex_format, data_format, data );
  }
}

void update_index_buffer(points::render::buffer_t *buffer)
{
  GLuint ibo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto size = points::render::buffer_size(buffer);
  auto offset = points::render::buffer_offset(buffer);
  auto data = points::render::buffer_get(buffer);
  if (size)
  {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, size, data);
  }
}

void update_vertex_buffer(points::render::buffer_t *buffer)
{
  GLuint vbo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto size = points::render::buffer_size(buffer);
  auto offset = points::render::buffer_offset(buffer);
  auto data = points::render::buffer_get(buffer);
  if (size)
  {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, offset, size, data);
  }
}

void update_texture_buffer(points::render::buffer_t *buffer)
{
  auto texture_type = points::render::buffer_texture_type(buffer);
  GLuint texture = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto width = points::render::buffer_width(buffer);
  auto height = points::render::buffer_height(buffer);
  auto data_format = format_to_glformat(points::render::buffer_format(buffer));
  GLenum tex_format = component_to_tex_format(points::render::buffer_components(buffer));

  switch (texture_type)
  {
  case points::render::buffer_texture_2d:
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, tex_format, data_format,
                 points::render::buffer_get(buffer));
    break;
  case points::render::buffer_texture_cubemap_positive_x:
  case points::render::buffer_texture_cubemap_negative_x:
  case points::render::buffer_texture_cubemap_positive_y:
  case points::render::buffer_texture_cubemap_negative_y:
  case points::render::buffer_texture_cubemap_positive_z:
  case points::render::buffer_texture_cubemap_negative_z:
    //int diff = int(texture_type) - int(points::render::buffer_texture_cubemap_positive_x);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glTexImage2D(GL_TEXTURE_CUBE_MAP, 0, GL_RGB, width, height, 0, tex_format, data_format,
                 points::render::buffer_get(buffer));
  }

}

void remove_index_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &index_buffers)
{
  GLuint ibo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto it = std::find(index_buffers.begin(), index_buffers.end(), ibo);
  if (it == index_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete ibo {} but its not registed in index list.\n", ibo);
    return;
  }

  index_buffers.erase(it);
  glDeleteBuffers(1, &ibo);
}

void remove_vertex_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &vertex_buffers)
{
  GLuint vbo = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto it = std::find(vertex_buffers.begin(), vertex_buffers.end(), vbo);
  if (it == vertex_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete vbo {} but its not registed in vertex list.\n", vbo);
    return;
  }

  vertex_buffers.erase(it);
  glDeleteBuffers(1, &vbo);
}

void remove_texture_buffer(points::render::buffer_t *buffer, std::vector<GLuint> &texture_buffers)
{
  GLuint texture = cast_to_uint(points::render::buffer_user_ptr(buffer));
  auto it = std::find(texture_buffers.begin(), texture_buffers.end(), texture);
  if (it == texture_buffers.end())
  {
    fmt::print(stderr, "illegal state. Trying to delete texture {} but its not registed in texture list.\n", texture);
    return;
  }

  texture_buffers.erase(it);
  glDeleteTextures(1, &texture);
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
    default:
      fmt::print(stderr, "Missing gl handler!.");
      abort();
    }
  }
}

void gl_renderer::dirty_callback_static(struct points::render::renderer_t *renderer, void *user_ptr)
{
  static_cast<gl_renderer *>(user_ptr)->dirty_callback(renderer);
}

void gl_renderer::create_buffer_static(points::render::renderer_t *renderer, void *user_ptr, points::render::buffer_t *buffer)
{
 static_cast<gl_renderer *>(user_ptr)->create_buffer(renderer, buffer);
}

void gl_renderer::initialize_buffer_static(points::render::renderer_t *renderer, void *user_ptr, points::render::buffer_t *buffer)
{
  static_cast<gl_renderer *>(user_ptr)->initialize_buffer(renderer, buffer);
}

void gl_renderer::modify_buffer_static(points::render::renderer_t *renderer, void *user_ptr, points::render::buffer_t *buffer)
{
  static_cast<gl_renderer *>(user_ptr)->modify_buffer(renderer, buffer);
}

void gl_renderer::destroy_buffer_static(points::render::renderer_t *renderer, void *user_ptr, points::render::buffer_t *buffer)
{
  static_cast<gl_renderer *>(user_ptr)->destroy_buffer(renderer, buffer);
}

void gl_renderer::dirty_callback(points::render::renderer_t *r)
{
  (void) r;
}

void gl_renderer::create_buffer(points::render::renderer_t *r, points::render::buffer_t *buffer)
{
  (void) r;
  auto buffer_type = points::render::buffer_type(buffer);
  switch(buffer_type)
  {
    case points::render::buffer_type_index:
      create_index_buffer(buffer, index_buffers);
    break;
    case points::render::buffer_type_vertex:
      create_vertex_buffer(buffer, vertex_buffers);
    break;
    case points::render::buffer_type_uniform:
      break;
    case points::render::buffer_type_texture:
      create_texure_buffer(buffer, texture_buffers);
      break;
  }
}

void gl_renderer::initialize_buffer(points::render::renderer_t *r, points::render::buffer_t *buffer)
{
  (void) r;
  auto buffer_type = points::render::buffer_type(buffer);
  switch(buffer_type)
  {
    case points::render::buffer_type_index:
      initialize_index_buffer(buffer);
    break;
    case points::render::buffer_type_vertex:
      initialize_vertex_buffer(buffer);
    break;
    case points::render::buffer_type_uniform:
      break;
    case points::render::buffer_type_texture:
      initialize_texture_buffer(buffer);
      break;
  }
}

void gl_renderer::modify_buffer(points::render::renderer_t *r, points::render::buffer_t *buffer)
{
  (void) r;
  auto buffer_type = points::render::buffer_type(buffer);
  switch(buffer_type)
  {
    case points::render::buffer_type_index:
      update_index_buffer(buffer);
    break;
    case points::render::buffer_type_vertex:
      update_vertex_buffer(buffer);
    break;
    case points::render::buffer_type_uniform:
      break;
    case points::render::buffer_type_texture:
      update_texture_buffer(buffer);
      break;
  }

}

void gl_renderer::destroy_buffer(points::render::renderer_t *r, points::render::buffer_t *buffer)
{
  (void) r;
  auto buffer_type = points::render::buffer_type(buffer);
  switch(buffer_type)
  {
    case points::render::buffer_type_index:
      remove_index_buffer(buffer, index_buffers);
    break;
    case points::render::buffer_type_vertex:
      remove_vertex_buffer(buffer, vertex_buffers);
    break;
    case points::render::buffer_type_uniform:
      break;
    case points::render::buffer_type_texture:
      remove_texture_buffer(buffer, texture_buffers);
      break;
  }

}
