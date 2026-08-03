/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#ifndef DEW_RENDERER_H
#define DEW_RENDERER_H

#include <dew/common/format.h>
#include <dew/render/export.h>
#include <dew/render/buffer.h>
#include <dew/render/draw_group.h>

#ifdef __cplusplus
extern "C" {
#endif

enum dew_buffer_type_t
{
  dew_buffer_type_vertex,
  dew_buffer_type_index,
  dew_buffer_type_uniform
};

enum dew_texture_type_t
{
  dew_texture_type_2d,
  dew_texture_type_3d,
  dew_texture_type_cubemap,
  dew_texture_type_cubemap_positive_x,
  dew_texture_type_cubemap_negative_x,
  dew_texture_type_cubemap_positive_y,
  dew_texture_type_cubemap_negative_y,
  dew_texture_type_cubemap_positive_z,
  dew_texture_type_cubemap_negative_z
};

enum dew_aabb_mesh_buffer_mapping_t
{
  dew_aabb_bm_camera,
  dew_aabb_bm_color,
  dew_aabb_bm_position,
  dew_aabb_bm_index
};

enum dew_skybox_buffer_mapping_t
{
  dew_skybox_bm_inverse_view_projection,
  dew_skybox_bm_camera_pos,
  dew_skybox_bm_vertex,
  dew_skybox_bm_cube_map_texture
};

enum dew_buffer_mapping_t
{
  dew_bm_vertex,
  dew_bm_camera,
  dew_bm_color
};

enum dew_dyn_points_buffer_mapping_t
{
  dew_dyn_points_bm_vertex,
  dew_dyn_points_bm_color,
  dew_dyn_points_bm_camera,
  dew_dyn_points_bm_old_color,
  dew_dyn_points_bm_params,
  dew_dyn_points_bm_replevel
};

enum dew_axis_gizmo_buffer_mapping_t
{
  dew_axis_gizmo_bm_camera,
  dew_axis_gizmo_bm_color,
  dew_axis_gizmo_bm_position
};

enum dew_origin_anchor_buffer_mapping_t
{
  dew_origin_anchor_bm_camera,
  dew_origin_anchor_bm_color,
  dew_origin_anchor_bm_position,
  dew_origin_anchor_bm_index
};

enum dew_environment_buffer_mapping_t
{
  dew_environment_bm_inverse_view_projection,
  dew_environment_bm_camera_pos,
  dew_environment_bm_vertex,
  dew_environment_bm_params
};

enum dew_node_bbox_buffer_mapping_t
{
  dew_node_bbox_bm_camera,
  dew_node_bbox_bm_position,
  dew_node_bbox_bm_color
};

//= py.skip
struct dew_frame_t
{
  //= arrays: to_render[to_render_size]
  struct dew_draw_group_t* to_render;
  int to_render_size;
};

struct dew_renderer_t;
typedef void (*dew_renderer_dirty_callback_t)(struct dew_renderer_t* renderer, void *renderer_user_ptr);

typedef void (*dew_renderer_create_buffer_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, enum dew_buffer_type_t buffer_type, void **buffer_user_ptr);
typedef void (*dew_renderer_initialize_buffer_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, struct dew_buffer_t *buffer, void *buffer_user_ptr, enum dew_type_t type, enum dew_components_t components, int buffer_size, void *data);
typedef void (*dew_renderer_modify_buffer_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, struct dew_buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data);
typedef void (*dew_renderer_destroy_buffer_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, void *buffer_user_ptr);

typedef void (*dew_renderer_create_texture_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, enum dew_texture_type_t buffer_texture_type, void **buffer_user_ptr);
typedef void (*dew_renderer_initialize_texture_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, struct dew_buffer_t *buffer, void *texture_user_ptr, enum dew_texture_type_t buffer_texture_type, enum dew_type_t type, enum dew_components_t components, int size[3], void *data);
typedef void (*dew_renderer_modify_texture_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, struct dew_buffer_t *buffer, void *texture_user_ptr, enum dew_texture_type_t buffer_texture_type, int offset[3], int size[3], void *data);
typedef void (*dew_renderer_destroy_texture_t)(struct dew_renderer_t *renderer, void *renderer_user_ptr, void *texture_user_ptr);

struct dew_renderer_callbacks_t
{
  dew_renderer_dirty_callback_t dirty;

  dew_renderer_create_buffer_t create_buffer;
  dew_renderer_initialize_buffer_t initialize_buffer;
  dew_renderer_modify_buffer_t modify_buffer;
  dew_renderer_destroy_buffer_t destroy_buffer;

  dew_renderer_create_texture_t create_texture;
  dew_renderer_initialize_texture_t initialize_texture;
  dew_renderer_modify_texture_t modify_texture;
  dew_renderer_destroy_texture_t destroy_texture;
};

DEW_RENDER_EXPORT struct dew_renderer_t* dew_renderer_create(void);
DEW_RENDER_EXPORT void dew_renderer_destroy(struct dew_renderer_t *renderer);
DEW_RENDER_EXPORT void dew_renderer_add_camera(struct dew_renderer_t* renderer, struct dew_camera_t* camera);
//= py.no_keep_alive
DEW_RENDER_EXPORT void dew_renderer_remove_camera(struct dew_renderer_t* renderer, struct dew_camera_t* camera);
//= py.skip
DEW_RENDER_EXPORT struct dew_frame_t dew_renderer_frame(struct dew_renderer_t* renderer, struct dew_camera_t* camera);
//= py.skip
DEW_RENDER_EXPORT void dew_renderer_set_callback(struct dew_renderer_t* renderer, struct dew_renderer_callbacks_t callbacks, void *user_ptr);
DEW_RENDER_EXPORT void dew_renderer_add_data_source(struct dew_renderer_t *renderer, struct dew_data_source_t data_source);
//= py.no_keep_alive
DEW_RENDER_EXPORT void dew_renderer_remove_data_source(struct dew_renderer_t *renderer, struct dew_data_source_t data_source);

//= py.skip
DEW_RENDER_EXPORT void dew_to_render_add_render_group(struct dew_to_render_t *to_render, struct dew_draw_group_t draw_group);

#ifdef __cplusplus
}
#endif
#endif //DEW_RENDERER_H
