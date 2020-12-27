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
#include <points/render/renderer.h>
#include <points/render/buffer.h>
#include "buffer_p.h"

namespace points
{
namespace render
{
void buffer_remove_ref(struct buffer_t *buffer)
{
  buffer->ref = false;
}

void *buffer_user_ptr(struct buffer_t *buffer)
{
  return buffer->user_ptr;
}
void buffer_set_user_ptr(struct buffer_t *buffer, void *ptr)
{
  buffer->user_ptr = ptr;
}

void buffer_set_rendered(struct buffer_t *buffer)
{
  buffer->rendered = true;
}

const void *buffer_get(struct buffer_t *buffer)
{
  return buffer->data;
}

int buffer_size(struct buffer_t *buffer)
{
  return buffer->data_size;
}

int buffer_offset(struct buffer_t *buffer)
{
  return buffer->data_offset;
}

enum buffer_type_t buffer_type(struct buffer_t *buffer)
{
  return buffer->type;
}

enum buffer_format_t buffer_format(struct buffer_t *buffer)
{
  return buffer->format;
}

enum buffer_components_t buffer_components(struct buffer_t *buffer)
{
  return buffer->components;
}

enum buffer_normalize_t buffer_normalize(struct buffer_t *buffer)
{
  return buffer->normalize;
}

int buffer_mapping(struct buffer_t *buffer)
{
  return buffer->buffer_mapping;
}

}
} // namespace points
