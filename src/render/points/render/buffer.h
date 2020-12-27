/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#ifndef POINTS_BUFFER_H
#define POINTS_BUFFER_H

#include <points/render/export.h>

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
};
enum buffer_format_t
{
  u8,
  u16,
  u32,
  r32,
  r64,
};

enum buffer_components_t
{
  component_1 = 1,
  component_2 = 2,
  component_3 = 3,
  component_4 = 4
};

enum buffer_normalize_t
{
  do_not_normalize,
  normalize
};

struct buffer_t;
POINTS_RENDER_EXPORT void buffer_remove_ref(struct buffer_t *buffer);
POINTS_RENDER_EXPORT void *buffer_user_ptr(struct buffer_t *buffer);
POINTS_RENDER_EXPORT void buffer_set_user_ptr(struct buffer_t *buffer, void *ptr);
POINTS_RENDER_EXPORT void buffer_set_rendered(struct buffer_t *buffer);
POINTS_RENDER_EXPORT const void *buffer_get(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_size(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_offset(struct buffer_t *buffer);
POINTS_RENDER_EXPORT enum buffer_type_t buffer_type(struct buffer_t *buffer);
POINTS_RENDER_EXPORT enum buffer_format_t buffer_format(struct buffer_t *buffer);
POINTS_RENDER_EXPORT enum buffer_components_t buffer_components(struct buffer_t *buffer);
POINTS_RENDER_EXPORT enum buffer_normalize_t buffer_normalize(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_mapping(struct buffer_t *buffer);

}
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_BUFFER_H
