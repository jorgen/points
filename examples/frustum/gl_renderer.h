#ifndef FRUSTUM_GL_RENDERER_H
#define FRUSTUM_GL_RENDERER_H

#include <points/render/camera.h>
#include <points/render/renderer.h>

#include "include/glad/glad.h"

#include <vector>

struct gl_buffer_t
{
  GLuint id = 0;
  points::render::buffer_t *buffer;
  points::render::buffer_type_t buffer_type;
  points::type_t type;
  points::components_t components;
  void *data= nullptr;
  int data_size= 0;
  bool data_needs_upload = false;
  bool data_is_update = false;
};

struct gl_texture_t
{
  GLuint id = 0;
  points::render::buffer_t *buffer;
  points::render::texture_type_t texture_type;
  points::type_t type;
  points::components_t components;
  int size[3];
};

class gl_frame_handler
{
public:
  virtual ~gl_frame_handler();

  virtual void initialize() = 0;
  virtual void draw(points::render::draw_group_t &group) = 0;
};

class gl_aabb_handler : public gl_frame_handler
{
public:
  gl_aabb_handler();
  ~gl_aabb_handler();
  void initialize() override; 
  void draw(points::render::draw_group_t &group) override;

  GLuint vao;
  GLuint program;
  GLint attrib_position;
  GLint attrib_color;
  GLint uniform_pv;
  bool is_initialized;
};

class gl_flat_points_handler : public gl_frame_handler
{
public:
  gl_flat_points_handler();
  ~gl_flat_points_handler();
  void initialize() override; 
  void draw(points::render::draw_group_t &group) override;

  GLuint vao;
  GLuint program;
  GLint attrib_position;
  GLint attrib_color;
  GLint uniform_camera;
  bool is_initialized;
};

class gl_dyn_points_handler : public gl_frame_handler
{
public:
  gl_dyn_points_handler();
  ~gl_dyn_points_handler();
  void initialize() override; 
  void draw(points::render::draw_group_t &group) override;

  GLuint vao;
  GLuint program;
  GLint vertex_position;
  GLint rgb_position;
  GLint uniform_camera;
  bool is_initialized;
};

class gl_skybox_handler: public gl_frame_handler
{
public:
  gl_skybox_handler();
  ~gl_skybox_handler();
  void initialize() override; 
  void draw(points::render::draw_group_t &group) override;

  GLuint vao;
  GLuint program;
  GLint attrib_vertex;
  GLint uniform_inverse_vp;
  GLint uniform_skybox;
  GLint uniform_camera_pos;
  bool is_initialized;
};

  enum class clear
{
  none = 0,
  color = 1 << 0,
  depth = 1 << 1
};

class gl_renderer
{

public:
  gl_renderer(points::render::renderer_t *renderer, points::render::camera_t *camera);

  void draw(clear clear, int viewport_width, int viewport_height);

private:
  static void static_dirty_callback(struct points::render::renderer_t *renderer, void *renderer_user_ptr);
  static void static_create_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, enum points::render::buffer_type_t buffer_type, void **buffer_user_ptr);
  static void static_initialize_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *buffer_user_ptr, enum points::type_t type, enum points::components_t components, int buffer_size, void *data);
  static void static_modify_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data);
  static void static_destroy_buffer(struct points::render::renderer_t *renderer, void *renderer_user_ptr, void *buffer_user_ptr);
  static void static_create_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, enum points::render::texture_type_t buffer_texture_type, void **buffer_user_ptr);
  static void static_initialize_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, enum points::type_t type, enum points::components_t components, int size[3], void *data);
  static void static_modify_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, int offset[3], int size[3], void *data);
  static void static_destroy_texture(struct points::render::renderer_t *renderer, void *renderer_user_ptr, void *texture_user_ptr);
  void dirty_callback();
  void create_buffer(enum points::render::buffer_type_t buffer_type, void **buffer_user_ptr);
  void initialize_buffer(struct points::render::buffer_t *buffer, void *buffer_user_ptr, enum points::type_t type, enum points::components_t components, int buffer_size, void *data);
  void modify_buffer(struct points::render::buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data);
  void destroy_buffer(void *buffer_user_ptr);
  void create_texture(enum points::render::texture_type_t buffer_texture_type, void **buffer_user_ptr);
  void initialize_texture(struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, enum points::type_t type, enum points::components_t components, int size[3], void *data);
  void modify_texture(struct points::render::buffer_t *buffer, void *texture_user_ptr, enum points::render::texture_type_t buffer_texture_type, int offset[3], int size[3], void *data);
  void destroy_texture(void *texture_user_ptr);

  points::render::renderer_t *renderer;
  points::render::camera_t *camera;
  std::vector<gl_frame_handler *> frame_handlers;
  std::vector<gl_buffer_t *> index_buffers;
  std::vector<gl_buffer_t *> vertex_buffers;
  std::vector<gl_buffer_t *> uniform_buffers;
  std::vector<gl_texture_t *> texture_buffers;
  gl_aabb_handler aabb_handler;
  gl_skybox_handler skybox_handler;
  gl_flat_points_handler points_handler;
  gl_dyn_points_handler dynpoints_handler;
};

#endif //FRUSTUM_GL_RENDERER_H
