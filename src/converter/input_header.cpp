/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#include "input_header_p.h"

#include "morton_p.h"

namespace points
{
namespace converter
{
void header_set_point_count(header_t *header, uint64_t count)
{
  header->point_count = count;
}

void header_set_data_start(header_t *header, uint64_t offset)
{
  header->data_start = offset;
}

void header_set_coordinate_offset(header_t *header, double offset[3])
{
  memcpy(header->offset, offset, sizeof(header->offset));
}

void header_set_coordinate_scale(header_t *header, double scale[3])
{
  memcpy(header->scale, scale, sizeof(header->scale));
}

void header_set_aabb(header_t *header, double min[3], double max[3])
{
  memcpy(header->min, min, sizeof(header->min));
  memcpy(header->max, max, sizeof(header->max));
}

void header_add_attribute(header_t *header, const char *name, uint64_t name_size, format_t format, components_t components, int group)
{
  header->attribute_names.emplace_back(new char[name_size + 1]);
  memcpy(header->attribute_names.back().get(), name, name_size);
  header->attribute_names.back().get()[name_size] = 0;
  header->attributes.push_back({header->attribute_names.back().get(), name_size, format, components, group});
}

void header_p_calculate_morton_aabb(header_t &header)
{
  uint64_t max[3];
  max[0] = uint64_t(header.max[0] * header.scale[0]);
  max[1] = uint64_t(header.max[1] * header.scale[1]);
  max[2] = uint64_t(header.max[2] * header.scale[2]);
 
  uint64_t min[3];
  min[0] = uint64_t(header.min[0] * header.scale[0]);
  min[1] = uint64_t(header.min[1] * header.scale[1]);
  min[2] = uint64_t(header.min[2] * header.scale[2]);
}

}
} // namespace points
