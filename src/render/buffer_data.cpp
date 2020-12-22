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
#include "buffer_data_p.h"

namespace points
{
namespace render
{
void buffer_data_remove_ref(struct buffer_data *buffer)
{
  buffer->ref = false;
}

void buffer_data_set_rendered(struct buffer_data *buffer)
{
  buffer->rendered = true;
}

const void *buffer_data_get(struct buffer_data *buffer)
{
  return buffer->data;
}

int buffer_data_size(struct buffer_data *buffer)
{
  return buffer->data_size;
}

int buffer_data_offset(struct buffer_data *buffer)
{
  return buffer->data_offset;
}

}
} // namespace points
