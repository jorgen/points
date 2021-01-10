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
  buffer_type_uniform,
  buffer_type_texture
};
enum buffer_format_t
{
  buffer_format_u8,
  buffer_format_u16,
  buffer_format_u32,
  buffer_format_r32,
  buffer_format_r64
};

enum buffer_components_t
{
  component_1 = 1,
  component_2 = 2,
  component_3 = 3,
  component_4 = 4,
  component_4x4 = 5
};

enum buffer_texture_type_t
{
  buffer_texture_2d,
  buffer_texture_cubemap,
  buffer_texture_cubemap_positive_x,
  buffer_texture_cubemap_negative_x,
  buffer_texture_cubemap_positive_y,
  buffer_texture_cubemap_negative_y,
  buffer_texture_cubemap_positive_z,
  buffer_texture_cubemap_negative_z,
};

enum buffer_normalize_t
{
  buffer_normalize_do_not_normalize,
  buffer_normalize_normalize
};


struct buffer_t;
POINTS_RENDER_EXPORT void buffer_remove_ref(struct buffer_t *buffer);
POINTS_RENDER_EXPORT void *buffer_user_ptr(struct buffer_t *buffer);
POINTS_RENDER_EXPORT void buffer_set_user_ptr(struct buffer_t *buffer, void *ptr);
POINTS_RENDER_EXPORT void buffer_set_rendered(struct buffer_t *buffer);
POINTS_RENDER_EXPORT const void *buffer_get(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_size(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_offset(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_width(struct buffer_t *buffer);
POINTS_RENDER_EXPORT int buffer_height(struct buffer_t *buffer);
POINTS_RENDER_EXPORT enum buffer_texture_type_t buffer_texture_type(struct buffer_t *buffer);
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
