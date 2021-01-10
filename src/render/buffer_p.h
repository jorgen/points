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
#include <atomic>
#include <cstdint>
#include <vector>

#include <points/render/buffer.h>

namespace points
{
namespace render
{
  struct buffer_t
  {
    bool ref = true;
    bool rendered = false;
    const void *data = nullptr;
    int data_size = 0;
    int data_offset = 0;
    int width = 0;
    int height = 0;
    buffer_type_t type;
    buffer_format_t format;
    buffer_components_t components;
    buffer_normalize_t normalize;
    buffer_texture_type_t texture_type;
    int buffer_mapping;
    const struct buffer_t *parent_buffer = nullptr;
    void *user_ptr = nullptr;
  };
}
} // namespace points
