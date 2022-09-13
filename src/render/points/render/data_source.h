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
#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <points/render/draw_group.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace render
{
struct frame_camera_t
{
  double view[4][4];
  double projection[4][4];
  double view_projection[4][4];
  double inverse_view[4][4];
  double inverse_projection[4][4];
  double inverse_view_projection[4][4];
};

struct to_render_t;
typedef void (*add_to_frame_t)(frame_camera_t *camera, to_render_t *to_render, void *data);

struct data_source_t
{
  void *user_ptr;
  add_to_frame_t add_to_frame;
};

}
}
#ifdef __cplusplus
}
#endif

#endif // DATA_SOURCE_H
