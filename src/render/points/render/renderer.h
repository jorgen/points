#ifndef POINTS_RENDERER_H
#define POINTS_RENDERER_H

#include <points/export.h>
#include <points/format.h>
#include <points/render/aabb.h>
#include <points/render/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace render
{
enum buffer_type_t
{
  buffer_type_vertex,
  buffer_type_index,
  buffer_type_uniform
};

enum texture_type_t
{
  texture_type_2d,
  texture_type_3d,
  texture_type_cubemap,
  texture_type_cubemap_positive_x,
  texture_type_cubemap_negative_x,
  texture_type_cubemap_positive_y,
  texture_type_cubemap_negative_y,
  texture_type_cubemap_positive_z,
  texture_type_cubemap_negative_z
};

struct draw_buffer_t
{
  int buffer_mapping;
  void *user_ptr;
};

enum draw_type_t
{
  aabb_triangle_mesh,
  skybox_triangle,
  flat_points
};

enum aabb_mesh_buffer_mapping_t
{
  aabb_bm_camera,
  aabb_bm_color,
  aabb_bm_position,
  aabb_bm_index
};

enum skybox_buffer_mapping_t
{
  skybox_bm_inverse_view_projection,
  skybox_bm_camera_pos,
  skybox_bm_vertex,
  skybox_bm_cube_map_texture
};

enum points_buffer_mapping_t
{
  points_bm_camera,
  points_bm_vertex,
  points_bm_color
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
typedef void (*renderer_dirty_callback_t)(struct renderer_t* renderer, void *renderer_user_ptr);

typedef void (*renderer_create_buffer_t)(struct renderer_t *renderer, void *renderer_user_ptr, enum buffer_type_t buffer_type, void **buffer_user_ptr);
typedef void (*renderer_initialize_buffer_t)(struct renderer_t *renderer, void *renderer_user_ptr, struct buffer_t *buffer, void *buffer_user_ptr, enum type_t type, enum components_t components, int buffer_size, void *data);
typedef void (*renderer_modify_buffer_t)(struct renderer_t *renderer, void *renderer_user_ptr, struct buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data);
typedef void (*renderer_destroy_buffer_t)(struct renderer_t *renderer, void *renderer_user_ptr, void *buffer_user_ptr);

typedef void (*renderer_create_texture_t)(struct renderer_t *renderer, void *renderer_user_ptr, enum texture_type_t buffer_texture_type, void **buffer_user_ptr);
typedef void (*renderer_initialize_texture_t)(struct renderer_t *renderer, void *renderer_user_ptr, struct buffer_t *buffer, void *texture_user_ptr, enum texture_type_t buffer_texture_type, enum type_t type, enum components_t components, int size[3], void *data);
typedef void (*renderer_modify_texture_t)(struct renderer_t *renderer, void *renderer_user_ptr, struct buffer_t *buffer, void *texture_user_ptr, enum texture_type_t buffer_texture_type, int offset[3], int size[3], void *data);
typedef void (*renderer_destroy_texture_t)(struct renderer_t *renderer, void *renderer_user_ptr, void *texture_user_ptr);

struct renderer_callbacks_t
{
  renderer_dirty_callback_t dirty;

  renderer_create_buffer_t create_buffer;
  renderer_initialize_buffer_t initialize_buffer;
  renderer_modify_buffer_t modify_buffer;
  renderer_destroy_buffer_t destroy_buffer;

  renderer_create_texture_t create_texture;
  renderer_initialize_texture_t initialize_texture;
  renderer_modify_texture_t modify_texture;
  renderer_destroy_texture_t destroy_texture;
};

struct data_source_t;
POINTS_EXPORT struct renderer_t* renderer_create();
POINTS_EXPORT void renderer_destroy(struct renderer_t *renderer);
POINTS_EXPORT void renderer_add_camera(struct renderer_t* renderer, struct camera_t* camera);
POINTS_EXPORT void renderer_remove_camera(struct renderer_t* renderer, struct camera_t* camera);
POINTS_EXPORT struct frame_t renderer_frame(struct renderer_t* renderer, struct camera_t* camera);
POINTS_EXPORT void renderer_set_callback(struct renderer_t* renderer, renderer_callbacks_t callbacks, void *user_ptr);
POINTS_EXPORT void renderer_add_data_source(struct renderer_t *renderer, struct data_source_t *data_source);
POINTS_EXPORT void renderer_remove_data_source(struct renderer_t *renderer, struct data_source_t *data_source);
}
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_RENDERER_H
