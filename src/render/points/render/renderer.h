/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#ifndef POINTS_RENDERER_H
#define POINTS_RENDERER_H

#include <points/common/format.h>
#include <points/render/export.h>
#include <points/render/buffer.h>
#include <points/render/draw_group.h>

#ifdef __cplusplus
extern "C" {
#endif

enum points_buffer_type_t
{
  points_buffer_type_vertex,
  points_buffer_type_index,
  points_buffer_type_uniform
};

enum points_texture_type_t
{
  points_texture_type_2d,
  points_texture_type_3d,
  points_texture_type_cubemap,
  points_texture_type_cubemap_positive_x,
  points_texture_type_cubemap_negative_x,
  points_texture_type_cubemap_positive_y,
  points_texture_type_cubemap_negative_y,
  points_texture_type_cubemap_positive_z,
  points_texture_type_cubemap_negative_z
};

enum points_aabb_mesh_buffer_mapping_t
{
  points_aabb_bm_camera,
  points_aabb_bm_color,
  points_aabb_bm_position,
  points_aabb_bm_index
};

enum points_skybox_buffer_mapping_t
{
  points_skybox_bm_inverse_view_projection,
  points_skybox_bm_camera_pos,
  points_skybox_bm_vertex,
  points_skybox_bm_cube_map_texture
};

enum points_buffer_mapping_t
{
  points_bm_vertex,
  points_bm_camera,
  points_bm_color
};

enum points_dyn_points_buffer_mapping_t
{
  points_dyn_points_bm_vertex,
  points_dyn_points_bm_color,
  points_dyn_points_bm_camera,
  points_dyn_points_bm_old_color,
  points_dyn_points_bm_params
};

enum points_axis_gizmo_buffer_mapping_t
{
  points_axis_gizmo_bm_camera,
  points_axis_gizmo_bm_color,
  points_axis_gizmo_bm_position
};

enum points_origin_anchor_buffer_mapping_t
{
  points_origin_anchor_bm_camera,
  points_origin_anchor_bm_color,
  points_origin_anchor_bm_position,
  points_origin_anchor_bm_index
};

enum points_environment_buffer_mapping_t
{
  points_environment_bm_inverse_view_projection,
  points_environment_bm_camera_pos,
  points_environment_bm_vertex,
  points_environment_bm_params
};

enum points_node_bbox_buffer_mapping_t
{
  points_node_bbox_bm_camera,
  points_node_bbox_bm_position,
  points_node_bbox_bm_color
};

struct points_frame_t
{
  struct points_draw_group_t* to_render;
  int to_render_size;
};

struct points_renderer_t;
typedef void (*points_renderer_dirty_callback_t)(struct points_renderer_t* renderer, void *renderer_user_ptr);

typedef void (*points_renderer_create_buffer_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, enum points_buffer_type_t buffer_type, void **buffer_user_ptr);
typedef void (*points_renderer_initialize_buffer_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, struct points_buffer_t *buffer, void *buffer_user_ptr, enum points_type_t type, enum points_components_t components, int buffer_size, void *data);
typedef void (*points_renderer_modify_buffer_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, struct points_buffer_t *buffer, void *buffer_user_ptr, int offset, int buffer_size, void *data);
typedef void (*points_renderer_destroy_buffer_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, void *buffer_user_ptr);

typedef void (*points_renderer_create_texture_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, enum points_texture_type_t buffer_texture_type, void **buffer_user_ptr);
typedef void (*points_renderer_initialize_texture_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, struct points_buffer_t *buffer, void *texture_user_ptr, enum points_texture_type_t buffer_texture_type, enum points_type_t type, enum points_components_t components, int size[3], void *data);
typedef void (*points_renderer_modify_texture_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, struct points_buffer_t *buffer, void *texture_user_ptr, enum points_texture_type_t buffer_texture_type, int offset[3], int size[3], void *data);
typedef void (*points_renderer_destroy_texture_t)(struct points_renderer_t *renderer, void *renderer_user_ptr, void *texture_user_ptr);

struct points_renderer_callbacks_t
{
  points_renderer_dirty_callback_t dirty;

  points_renderer_create_buffer_t create_buffer;
  points_renderer_initialize_buffer_t initialize_buffer;
  points_renderer_modify_buffer_t modify_buffer;
  points_renderer_destroy_buffer_t destroy_buffer;

  points_renderer_create_texture_t create_texture;
  points_renderer_initialize_texture_t initialize_texture;
  points_renderer_modify_texture_t modify_texture;
  points_renderer_destroy_texture_t destroy_texture;
};

POINTS_RENDER_EXPORT struct points_renderer_t* points_renderer_create(void);
POINTS_RENDER_EXPORT void points_renderer_destroy(struct points_renderer_t *renderer);
POINTS_RENDER_EXPORT void points_renderer_add_camera(struct points_renderer_t* renderer, struct points_camera_t* camera);
POINTS_RENDER_EXPORT void points_renderer_remove_camera(struct points_renderer_t* renderer, struct points_camera_t* camera);
POINTS_RENDER_EXPORT struct points_frame_t points_renderer_frame(struct points_renderer_t* renderer, struct points_camera_t* camera);
POINTS_RENDER_EXPORT void points_renderer_set_callback(struct points_renderer_t* renderer, struct points_renderer_callbacks_t callbacks, void *user_ptr);
POINTS_RENDER_EXPORT void points_renderer_add_data_source(struct points_renderer_t *renderer, struct points_data_source_t data_source);
POINTS_RENDER_EXPORT void points_renderer_remove_data_source(struct points_renderer_t *renderer, struct points_data_source_t data_source);

POINTS_RENDER_EXPORT void points_to_render_add_render_group(struct points_to_render_t *to_render, struct points_draw_group_t draw_group);

#ifdef __cplusplus
}
#endif
#endif //POINTS_RENDERER_H
