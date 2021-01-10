#ifndef POINTS_RENDERER_H
#define POINTS_RENDERER_H

#include <points/render/export.h>
#include <points/render/aabb.h>
#include <points/render/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace render
{
struct buffer_t;
struct draw_buffer_t
{
  buffer_t *data;
  void *user_ptr;
};

enum draw_type_t
{
  aabb_triangle_mesh,
  skybox_triangle
};

enum aabb_triangle_mesh_buffer_mapping_t
{
  aabb_triangle_mesh_camera,
  aabb_triangle_mesh_color,
  aabb_triangle_mesh_position
};

enum skybox_buffer_mapping_t
{
  skybox_inverse_view_projection,
  skybox_camera_pos,
  skybox_vertex,
  skybox_texture_cube,
};

struct draw_group_t
{
  draw_type_t draw_type;
  struct draw_buffer_t *buffers;
  int buffers_size;
  int draw_size;
};

struct frame_t
{
  struct draw_group_t* to_render;
  int to_render_size;
};

struct renderer_t;
typedef void (*renderer_dirty_callback_t)(struct renderer_t* renderer, void *user_ptr);
typedef void (*renderer_create_buffer_t)(struct renderer_t *renderer, void *user_ptr, struct buffer_t *buffer);
typedef void (*renderer_initialize_buffer_t)(struct renderer_t *renderer, void *user_ptr, struct buffer_t *buffer);
typedef void (*renderer_modify_buffer_t)(struct renderer_t *renderer, void *user_ptr, struct buffer_t *buffer);
typedef void (*renderer_destroy_buffer_t)(struct renderer_t *renderer, void *user_ptr, struct buffer_t *buffer);

struct renderer_callbacks_t
{
  renderer_dirty_callback_t dirty;
  renderer_create_buffer_t create_buffer;
  renderer_initialize_buffer_t initialize_buffer;
  renderer_modify_buffer_t modify_buffer;
  renderer_destroy_buffer_t destroy_buffer;
};

struct data_source_t;
POINTS_RENDER_EXPORT struct renderer_t* renderer_create();
POINTS_RENDER_EXPORT void renderer_destroy(struct renderer_t *renderer);
POINTS_RENDER_EXPORT void renderer_add_camera(struct renderer_t* renderer, struct camera_t* camera);
POINTS_RENDER_EXPORT void renderer_remove_camera(struct renderer_t* renderer, struct camera_t* camera);
POINTS_RENDER_EXPORT struct frame_t renderer_frame(struct renderer_t* renderer, struct camera_t* camera);
POINTS_RENDER_EXPORT void renderer_set_callback(struct renderer_t* renderer, renderer_callbacks_t callbacks, void *user_ptr);
POINTS_RENDER_EXPORT void renderer_add_data_source(struct renderer_t *renderer, struct data_source_t *data_source);
POINTS_RENDER_EXPORT void renderer_remove_data_source(struct renderer_t *renderer, struct data_source_t *data_source);
}
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_RENDERER_H
