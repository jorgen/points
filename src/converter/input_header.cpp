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
#include "input_header.hpp"

#include "morton.hpp"
#include "reader.hpp"

#include <assert.h>

namespace points
{
namespace converter
{
void attributes_add_attribute(struct attributes_t *attributes, const char *name, uint32_t name_size, enum type_t format, enum components_t components)
{
  attributes->attribute_names.emplace_back(new char[name_size + 1]);
  memcpy(attributes->attribute_names.back().get(), name, name_size);
  attributes->attribute_names.back().get()[name_size] = 0;
  attributes->attributes.push_back({attributes->attribute_names.back().get(), name_size, format, components});
}

void header_p_set_morton_aabb(const tree_global_state_t &tree_state, internal_header_t &header)
{
  double inv_scale = tree_state.inv_scale;

  uint64_t min[3];
  min[0] = uint64_t(round((header.min[0] - tree_state.offset[0]) * inv_scale));
  min[1] = uint64_t(round((header.min[1] - tree_state.offset[1]) * inv_scale));
  min[2] = uint64_t(round((header.min[2] - tree_state.offset[2]) * inv_scale));
  morton::encode(min, header.morton_min);
  uint64_t max[3];
  max[0] = uint64_t(round((header.max[0] - tree_state.offset[0]) * inv_scale));
  max[1] = uint64_t(round((header.max[1] - tree_state.offset[1]) * inv_scale));
  max[2] = uint64_t(round((header.max[2] - tree_state.offset[2]) * inv_scale));
  morton::encode(max, header.morton_max);

  //assert(!(header.morton_max < header.morton_min));
  header.lod_span = morton::morton_lod(header.morton_min, header.morton_max);
}

void attributes_copy(const attributes_t &source, attributes_t &target)
{
  assert(target.attributes.empty());
  assert(target.attribute_names.empty());
  target.attributes = source.attributes;
  for (int i = 0; i < int(source.attributes.size()); i++)
  {
    target.attribute_names.emplace_back(new char[source.attributes[i].name_size + 1]); 
    memcpy(target.attribute_names[i].get(), source.attribute_names[i].get(), source.attributes[i].name_size);
    target.attribute_names[i].get()[source.attributes[i].name_size] = 0;
    target.attributes[i].name = target.attribute_names[i].get();
  }
}

void attribute_buffers_initialize(const std::vector<std::pair<type_t, components_t>> &attributes_def, attribute_buffers_t &buffers, uint64_t point_count)
{
  buffers.data.reserve(attributes_def.size());
  buffers.buffers.reserve(attributes_def.size());
  for (auto &attribute : attributes_def)
  {
    uint64_t buffer_size = size_for_format(attribute.first) * uint64_t(attribute.second) * point_count;
    buffers.data.emplace_back(new uint8_t[buffer_size]);
    buffers.buffers.push_back({buffers.data.back().get(), buffer_size});
  }
}

void attribute_buffers_adjust_buffers_to_size(const std::vector<std::pair<type_t, components_t>> &attributes_def, attribute_buffers_t &buffers, uint64_t point_count)
{
  assert(attributes_def.size() == buffers.buffers.size());

  for (int i = 0; i < int(attributes_def.size()); i++)
  {
    auto &buffer = buffers.buffers[i];
    buffer.size = size_for_format(attributes_def[i].first) * uint64_t(attributes_def[i].second) * point_count;
  }
}
}
} // namespace points
