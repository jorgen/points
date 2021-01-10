#ifndef FRUSTUM_GL_RENDERER_H
#define FRUSTUM_GL_RENDERER_H

#include <points/render/camera.h>
#include <points/render/renderer.h>

#include "include/glad/glad.h"

#include <vector>

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

class gl_skybox_handler: public gl_frame_handler
{
public:
  gl_skybox_handler();
  ~gl_skybox_handler();
  void initialize() override; 
  void draw(points::render::draw_group_t &group) override;

  GLuint vao;
  GLuint program;
  GLint attrib_position;
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
  static void dirty_callback_static(struct points::render::renderer_t* r, void *user_ptr);
  static void create_buffer_static(struct points::render::renderer_t *r, void *user_ptr, struct points::render::buffer_t *buffer);
  static void initialize_buffer_static(struct points::render::renderer_t *r, void *user_ptr, struct points::render::buffer_t *buffer);
  static void modify_buffer_static(struct points::render::renderer_t *r, void *user_ptr, struct points::render::buffer_t *buffer);
  static void destroy_buffer_static(struct points::render::renderer_t *r, void *user_ptr, struct points::render::buffer_t *buffer);
  void dirty_callback(struct points::render::renderer_t* r);
  void create_buffer(struct points::render::renderer_t *r, struct points::render::buffer_t *buffer);
  void initialize_buffer(struct points::render::renderer_t *r, struct points::render::buffer_t *buffer);
  void modify_buffer(struct points::render::renderer_t *r, struct points::render::buffer_t *buffer);
  void destroy_buffer(struct points::render::renderer_t *r, struct points::render::buffer_t *buffer);

  points::render::renderer_t *renderer;
  points::render::camera_t *camera;
  std::vector<gl_frame_handler *> frame_handlers;
  std::vector<GLuint> index_buffers;
  std::vector<GLuint> vertex_buffers;
  std::vector<GLuint> texture_buffers;
  gl_aabb_handler aabb_handler;
  gl_skybox_handler skybox_handler;
};

#endif //FRUSTUM_GL_RENDERER_H
