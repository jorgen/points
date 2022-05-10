/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#include "attributes_configs.hpp"

#include "input_header.hpp"

#include <points/converter/default_attribute_names.h>
namespace points
{
namespace converter
{

attributes_configs_t::attributes_configs_t(const tree_global_state_t &global_state)
  : _global_state(global_state)
{
  (void) _global_state;
}

static bool compare_attribute(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  if (a.format != b.format)
    return false;
  if (a.components != b.components)
    return false;
  return memcmp(a.name, b.name, a.name_size) == 0;
}
static bool compare_attributes(const attributes_t &a, const attributes_t &b)
{
  if (a.attributes.size() != b.attributes.size())
    return false;
  for (int i = 1; i < int(a.attributes.size()); i++)
  {
    if (!compare_attribute(a.attributes[i], b.attributes[i]))
      return false;
  }
  return true;
}

attributes_id_t attributes_configs_t::get_attribute_config_index(attributes_t &&attr)
{
  std::unique_lock<std::mutex> lock(_mutex);
  for (int i = 0; i < int(_attributes_configs.size()); i++)
  {
    auto &source = _attributes_configs[i];
    if (compare_attributes(source.attributes, attr))
      return {i};
  }

  int ret = int(_attributes_configs.size());
  _attributes_configs.emplace_back();
  auto &new_config = _attributes_configs.back();
  new_config.attributes = std::move(attr);
  return {ret};
}

const attributes_t &attributes_configs_t::get(attributes_id_t id)
{
  std::unique_lock<std::mutex> lock(_mutex);
  return _attributes_configs[id.data].attributes;
}

#ifndef NDEBUG
static bool attributes_ids_increase(const attributes_id_t *begin, const attributes_id_t *end)
{
  if (begin == end)
    return true;

  auto prev_id = begin->data;
  begin++;
  while(begin < end)
  {
    if (begin->data <= prev_id)
      return false;
    prev_id = begin->data;
    begin++;
  }
  return true;
}
#endif

bool is_attribute_names_equal(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  return memcmp(a.name, b.name, a.name_size) == 0;
}

static void make_new_attributes(attribute_config_t &destination, const format_t point_type, const std::vector<attribute_config_t> &attributes_configs, const attributes_id_t *begin, const attributes_id_t *end)
{
  int count = int(end - begin);
  attributes_copy(attributes_configs[begin->data].attributes, destination.attributes);
  destination.extra_info.resize(destination.attributes.attributes.size());
  destination.child_attributes.reserve(count);
  destination.child_attributes.emplace_back(*begin);
  assert(std::string(POINTS_ATTRIBUTE_XYZ) == destination.attributes.attributes.front().name);
  assert(destination.attributes.attributes.front().components == components_1);
  destination.attributes.attributes.front().format = point_type;
  for (int i = 1; i < count; i++)
  {
    destination.child_attributes.emplace_back(begin[i]);
    auto &source = attributes_configs[begin[i].data];
    for (int attribute_index = 0; attribute_index < int(source.attributes.attributes.size()); attribute_index++)
    {
      auto &source_attrib = source.attributes.attributes[attribute_index];
      bool found = false;
      for (int dest_attrib_index = 0; dest_attrib_index < int(destination.attributes.attributes.size()); dest_attrib_index++)
      {
        auto &dest_attrib = destination.attributes.attributes[dest_attrib_index];
        if (is_attribute_names_equal(source_attrib, dest_attrib))
        {
          if (int(dest_attrib.format) < int(source_attrib.format))
            dest_attrib.format = source_attrib.format;
          if (int(dest_attrib.components) < int(source_attrib.components))
            dest_attrib.components = source_attrib.components;
          found = true;
        }
      }
      if (!found)
      {
        destination.attributes.attributes.push_back(source_attrib);
        destination.attributes.attribute_names.emplace_back(new char[source_attrib.name_size + 1]);
        memcpy(destination.attributes.attribute_names[i].get(), source_attrib.name, source_attrib.name_size);
        destination.attributes.attribute_names[i].get()[source_attrib.name_size] = 0;
        destination.attributes.attributes[i].name = destination.attributes.attribute_names[i].get();
      }
    }
  }

  destination.child_attribute_index_map.resize(count);
  for (int i = 0; i < count; i++)
  {
    destination.child_attribute_index_map[i].resize(destination.attributes.attributes.size(), -1);
  }
  for (int dest_attrib_index = 0; dest_attrib_index < int(destination.attributes.attributes.size()); dest_attrib_index++)
  {
    auto &dest_attrib = destination.attributes.attributes[dest_attrib_index];
    destination.extra_info[dest_attrib_index].is_accumulative = false;
    for (int i = 0; i < count; i++)
    {
      auto &source = attributes_configs[begin[i].data];
      for (int source_attrib_index = 0; source_attrib_index < int(source.attributes.attributes.size()); source_attrib_index++)
      {
        auto &source_attrib = source.attributes.attributes[source_attrib_index];
        if (is_attribute_names_equal(dest_attrib, source_attrib))
        {
          destination.child_attribute_index_map[i][dest_attrib_index] = source_attrib_index;
        }
      }
    }
  }
}

attributes_id_t attributes_configs_t::get_lod_attribute_id_for_input_attributes(const format_t point_type, const attributes_id_t *begin, const attributes_id_t *end)
{
  assert(begin < end);
  assert(attributes_ids_increase(begin, end));
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = std::find_if(_attributes_configs.begin(), _attributes_configs.end(), [begin, end](const attribute_config_t &config)
  {
    auto current = begin;
    for (int i = 0; i < int(config.child_attributes.size()) && current != end; ++current, i++)
    {
      if (current->data != config.child_attributes[i].data)
        return false;
    }
    if (current != end)
      return false;
    return true;
  });

  if (it != _attributes_configs.end())
    return { it - _attributes_configs.begin() };

  _attributes_configs.emplace_back();
  auto &config = _attributes_configs.back();
  make_new_attributes(config, point_type, _attributes_configs, begin, end);
  return {_attributes_configs.size() - 1};
}
}}
