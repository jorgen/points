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
  virtual void set_camera_state(points::render::camera *camera) = 0;
  virtual void draw(points::render::draw_group &group) = 0;
};

class gl_aabb_handler : public gl_frame_handler
{
public:
  gl_aabb_handler();
  ~gl_aabb_handler();
  void initialize() override; 
  void set_camera_state(points::render::camera *camera) override;
  void draw(points::render::draw_group &group) override;

  GLuint vao;
  GLuint program;
  GLint attrib_position;
  GLint attrib_color;
  GLint uniform_pv;
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
  gl_renderer(points::render::renderer *renderer, points::render::camera *camera);

  void draw(clear clear, int viewport_width, int viewport_height);

private:
  points::render::renderer *renderer;
  points::render::camera *camera;
  std::vector<gl_frame_handler *> frame_handlers;
  std::vector<GLuint> index_buffers;
  std::vector<GLuint> vertex_buffers;
  gl_aabb_handler aabb_handler;
};

#endif //FRUSTUM_GL_RENDERER_H
