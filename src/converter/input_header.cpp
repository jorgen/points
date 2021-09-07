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

#include <assert.h>

namespace points
{
namespace converter
{
void header_add_attribute(struct header_t *h, const char *name, uint64_t name_size, enum format_t format, enum components_t components, int group)
{
  internal_header_t *header = static_cast<internal_header_t *>(h);
  header->attributes.attribute_names.emplace_back(new char[name_size + 1]);
  memcpy(header->attributes.attribute_names.back().get(), name, name_size);
  header->attributes.attribute_names.back().get()[name_size] = 0;
  header->attributes.attributes.push_back({header->attributes.attribute_names.back().get(), name_size, format, components, group});
}

void header_set_name(header_t *h, const char *name, uint64_t name_size)
{
  internal_header_t *header = static_cast<internal_header_t *>(h);
  header->name = std::string(name, name_size);
}

const char *header_get_name(header_t *h)
{
  internal_header_t *header = static_cast<internal_header_t *>(h);
  return header->name.c_str();

}

void header_p_set_morton_aabb(const tree_global_state_t &tree_state, internal_header_t &header)
{
  double inv_scale[3];
  inv_scale[0] = 1 / tree_state.scale[0];
  inv_scale[1] = 1 / tree_state.scale[1];
  inv_scale[2] = 1 / tree_state.scale[2];

  uint64_t min[3];
  min[0] = uint64_t((header.min[0] + tree_state.offset[0]) * inv_scale[0]);
  min[1] = uint64_t((header.min[1] + tree_state.offset[1]) * inv_scale[1]);
  min[2] = uint64_t((header.min[2] + tree_state.offset[2]) * inv_scale[2]);
  morton::encode(min, header.morton_min);

  uint64_t max[3];
  max[0] = uint64_t((header.max[0] + tree_state.offset[0]) * inv_scale[0]);
  max[1] = uint64_t((header.max[1] + tree_state.offset[1]) * inv_scale[1]);
  max[2] = uint64_t((header.max[2] + tree_state.offset[2]) * inv_scale[2]);
  morton::encode(max, header.morton_max);

  header.lod_span = morton::morton_lod(header.morton_min, header.morton_max);
}

void attributes_copy(const attributes_t &source, attributes_t &target)
{
  assert(target.attributes.empty());
  assert(target.attribute_names.empty());
  target.attributes = source.attributes;
  for (int i = 0; i < int(source.attributes.size()); i++)
  {
    target.attribute_names.emplace_back(new char[source.attributes[i].name_size]); 
    memcpy(target.attribute_names[i].get(), source.attribute_names[i].get(), source.attributes[i].name_size);
    target.attributes[i].name = target.attribute_names[i].get();
  }
}

void header_copy(const internal_header_t &source, internal_header_t &target)
{
  target.name = source.name;
  target.point_count = source.point_count;
  memcpy(target.offset, source.offset, sizeof(target.offset));
  memcpy(target.scale, source.scale, sizeof(target.scale));
  memcpy(target.min, source.min, sizeof(target.min));
  memcpy(target.max, source.max, sizeof(target.max));
  memcpy(&target.morton_min, &source.morton_min, sizeof(target.morton_min));
  memcpy(&target.morton_max, &source.morton_max, sizeof(target.morton_max));
  attributes_copy(source.attributes, target.attributes);
}

void attribute_buffers_initialize(const std::vector<attribute_t> &attributes, attribute_buffers_t &buffers, uint64_t point_count)
{
  buffers.data.reserve(attributes.size());
  buffers.buffers.reserve(attributes.size());
  for (auto &attribute : attributes)
  {
    uint64_t buffer_size = size_for_format(attribute.format) * uint64_t(attribute.components) * point_count;
    buffers.data.emplace_back(new uint8_t[buffer_size]);
    buffers.buffers.push_back({buffers.data.back().get(), buffer_size});
  }
}

void attribute_buffers_adjust_buffers_to_size(const std::vector<attribute_t> &attributes, attribute_buffers_t &buffers, uint64_t point_count)
{
  assert(attributes.size() == buffers.buffers.size());

  for (int i = 0; i < int(attributes.size()); i++)
  {
    auto &attribute = attributes[i];
    auto &buffer = buffers.buffers[i];
    buffer.size = size_for_format(attribute.format) * uint64_t(attribute.components) * point_count;
  }
}

}
} // namespace points
