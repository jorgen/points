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
#include <points/render/buffer.h>
#include "buffer.hpp"


namespace points::render
{
void buffer_set_rendered(struct buffer_t *buffer)
{
  buffer->rendered = true;
}

void buffer_release_data(struct buffer_t *buffer)
{
  if (buffer->releaseBuffer)
    buffer->releaseBuffer();
}
}

