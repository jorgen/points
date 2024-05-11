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

void attributes_copy(const attributes_t &source, attributes_t &target)
{
  assert(target.attributes.empty());
  assert(target.attribute_names.empty());
  target.attributes = source.attributes;
  for (int i = 0; i < int(source.attributes.size()); i++)
  {
    auto &target_attrib_name = target.attribute_names.emplace_back(new char[source.attributes[i].name_size + 1]);
    memcpy(target_attrib_name.get(), source.attribute_names[i].get(), source.attributes[i].name_size);
    target.attribute_names[i].get()[source.attributes[i].name_size] = 0;
    target.attributes[i].name = target.attribute_names[i].get();
  }
}

void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count)
{
  buffers.data.reserve(attributes_def.size());
  buffers.buffers.reserve(attributes_def.size());
  for (auto &attribute : attributes_def)
  {
    uint32_t buffer_size = size_for_format(attribute.type) * uint32_t(attribute.components) * point_count;
    buffers.data.emplace_back(new uint8_t[buffer_size]);
    buffers.buffers.emplace_back(buffers.data.back().get(), buffer_size);
  }
}

void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count, std::unique_ptr<uint8_t[]> &&morton_attribute_buffer)
{
  buffers.data.reserve(attributes_def.size());
  buffers.buffers.reserve(attributes_def.size());
  bool first = true;
  for (auto &attribute : attributes_def)
  {
    uint32_t buffer_size = size_for_format(attribute.type) * uint64_t(attribute.components) * point_count;
    if (first)
    {
      buffers.data.emplace_back(std::move(morton_attribute_buffer));
      first = false;
    }
    else
    {
      buffers.data.emplace_back(new uint8_t[buffer_size]);
    }
    buffers.buffers.emplace_back(buffers.data.back().get(), buffer_size);
  }
}

void attribute_buffers_adjust_buffers_to_size(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count)
{
  assert(attributes_def.size() == buffers.buffers.size());

  for (int i = 0; i < int(attributes_def.size()); i++)
  {
    auto &buffer = buffers.buffers[i];
    buffer.size = size_for_format(attributes_def[i].type) * uint32_t(attributes_def[i].components) * point_count;
  }
}
} // namespace converter
} // namespace points
