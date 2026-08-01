/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#ifndef DRAW_GROUP_H
#define DRAW_GROUP_H

#include <dew/render/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_draw_buffer_t
{
  int buffer_mapping;
  void *user_ptr;
};

enum dew_draw_type_t
{
  dew_aabb_triangle_mesh,
  dew_skybox_triangle,
  dew_flat_points,
  dew_dyn_points_1,
  dew_dyn_points_3,
  dew_dyn_points_crossfade,
  dew_axis_gizmo_lines,
  dew_origin_anchor_mesh,
  dew_environment_bg,
  dew_node_bbox_lines,
};

struct dew_draw_group_t
{
  enum dew_draw_type_t draw_type;
  struct dew_draw_buffer_t *buffers;
  int buffers_size;
  int draw_size;
  int lod_level;
  // Per-point LOD constants for dyn_points (0 elsewhere). The vertex shader turns view depth gl_Position.w
  // into a continuous grid level: cells = lod_density_scale * w / lod_px_scale; W = log2(cells); a point is
  // kept iff its rep_level >= W (smooth screen-door). Per-frame constants (same for every node this frame):
  //   lod_px_scale     = projection[1][1] * 0.5 * viewport_height
  //   lod_density_scale = render_density_px / tree_config.scale
  float lod_px_scale;
  float lod_density_scale;
};

#ifdef __cplusplus
}
#endif
#endif // DRAW_GROUP_H
