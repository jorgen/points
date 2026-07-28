/************************************************************************
** Points - point cloud management software.
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

#include <points/render/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_draw_buffer_t
{
  int buffer_mapping;
  void *user_ptr;
};

enum points_draw_type_t
{
  points_aabb_triangle_mesh,
  points_skybox_triangle,
  points_flat_points,
  points_dyn_points_1,
  points_dyn_points_3,
  points_dyn_points_crossfade,
  points_axis_gizmo_lines,
  points_origin_anchor_mesh,
  points_environment_bg,
  points_node_bbox_lines,
};

struct points_draw_group_t
{
  enum points_draw_type_t draw_type;
  struct points_draw_buffer_t *buffers;
  int buffers_size;
  int draw_size;
  int lod_level;
  // Runtime per-node LOD fade-in (dyn_points steady draws only; 0 elsewhere). [0, lod_solid_count) is the
  // settled prefix drawn opaque; [lod_solid_count, draw_size) is the level being revealed, drawn with
  // lod_fade_alpha in [0,1] (screen-door dissolve). lod_fade_alpha >= 1 (or solid==draw_size) means no fade.
  int lod_solid_count;
  float lod_fade_alpha;
};

#ifdef __cplusplus
}
#endif
#endif // DRAW_GROUP_H
