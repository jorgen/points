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

namespace points
{
namespace render
{

struct draw_buffer_t
{
  int buffer_mapping;
  void *user_ptr;
};

enum draw_type_t
{
  aabb_triangle_mesh,
  skybox_triangle,
  flat_points,
  dyn_points_1,
  dyn_points_3,
};

struct draw_group_t
{
  draw_type_t draw_type;
  struct draw_buffer_t *buffers;
  int buffers_size;
  int draw_size;
};

} // namespace render
} // namespace points

#ifdef __cplusplus
}
#endif
#endif // DRAW_GROUP_H
