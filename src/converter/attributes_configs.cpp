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
#include "fixed_size_vector.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include <algorithm>

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
  for (int i = 0; i < int(a.attributes.size()); i++)
  {
    if (!compare_attribute(a.attributes[i], b.attributes[i]))
      return false;
  }
  return true;
}
static bool compare_attributrs_with_point_format(const attributes_t &a, type_t point_type, components_t point_components, const attributes_t &b)
{
  if (a.attributes.size() != b.attributes.size())
    return false;
  if (a.attributes.empty())
    return true;
  auto &b_point_attr = b.attributes.front();
  if (a.attributes.front().name_size != b_point_attr.name_size)
    return false;
  if (point_type != b_point_attr.format)
    return false;
  if (point_components!= b_point_attr.components)
    return false;
  if (memcmp(a.attributes.front().name, b_point_attr.name, b_point_attr.name_size) != 0)
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
      return {uint32_t(i)};
  }

  int ret = int(_attributes_configs.size());
  _attributes_configs.emplace_back();
  auto &new_config = _attributes_configs.back();
  new_config.attributes = std::move(attr);
  return {uint32_t(ret)};
}

attributes_id_t attributes_configs_t::get_attribute_for_point_format(attributes_id_t id, type_t type, components_t components)
{
  std::unique_lock<std::mutex> lock(_mutex);
  assert(id.data < _attributes_configs.size());
  if (_attributes_configs.capacity() == _attributes_configs.size())
  {
    _attributes_configs.reserve(_attributes_configs.size() * 2);
  }
  auto &attr = _attributes_configs[id.data];
  for (int i = 0; i < int(_attributes_configs.size()); i++)
  {
    auto &source = _attributes_configs[i];
    if (compare_attributrs_with_point_format(attr.attributes, type, components, source.attributes))
    {
      return {uint32_t(i)};
    }
  }
  int ret = int(_attributes_configs.size());
  _attributes_configs.emplace_back();
  auto &new_config = _attributes_configs.back();
  attributes_copy(attr.attributes, new_config.attributes);
  new_config.attributes.attributes[0].format = type;
  new_config.attributes.attributes[0].components = components;
  return {uint32_t(ret)};
}

static bool contains_attribute(const attributes_t &attributes, const attribute_t &attribute)
{
  for (auto &to_check_attrib :attributes.attributes)
  {
    if (to_check_attrib.name_size == attribute.name_size
        && memcmp(to_check_attrib.name, attribute.name, attribute.name_size) == 0)
    {
      return true;
    }
  }
  return false;
}

static void add_missing_attributes(const attributes_t &source, attributes_t &target)
{
  for (auto &source_attrib : source.attributes)
  {
    if (contains_attribute(target, source_attrib))
    {
        continue;
    }
    target.attributes.push_back(source_attrib);
    auto &target_attrib = target.attributes.back();
    target.attribute_names.emplace_back(new char[source_attrib.name_size + 1]);
    memcpy(target.attribute_names.back().get(), source_attrib.name, source_attrib.name_size);
    target.attribute_names.back().get()[source_attrib.name_size] = 0;
    target_attrib.name = target.attribute_names.back().get();
  }
}

attribute_lod_mapping_t attributes_configs_t::get_lod_attribute_mapping(int lod, const attributes_id_t *begin, const attributes_id_t *end)
{
  fixed_capacity_vector_t<attributes_id_t> attribute_ids_sorted(end - begin);
  memcpy(attribute_ids_sorted.begin(), begin, attribute_ids_sorted.capacity() * sizeof(attributes_id_t));
  auto attrib_begin = attribute_ids_sorted.begin();
  auto attrib_end = attribute_ids_sorted.end();
  std::sort(attrib_begin, attrib_end, [](const attributes_id_t &a, const attributes_id_t &b) { return a.data < b.data; });
  attrib_end = std::unique(attrib_begin, attrib_end, [](const attributes_id_t &a, const attributes_id_t &b) { return a.data == b.data; });

  auto lod_format = morton_format_from_lod(lod);
  attributes_t target;
  {
    std::unique_lock<std::mutex> lock(_mutex);
    attributes_copy(_attributes_configs[attrib_begin->data].attributes, target);
    auto it = attrib_begin;
    while (++it != attrib_end)
    {
      add_missing_attributes(_attributes_configs[it->data].attributes, target);
    }
    target.attributes.front().format = lod_format;
    target.attributes.front().components = components_1;
  }
  auto id = get_attribute_config_index(std::move(target));
  return get_lod_attribute_mapping(lod_format, id, attrib_begin, attrib_end);
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

static bool is_attribute_names_equal(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  return memcmp(a.name, b.name, a.name_size) == 0;
}

attribute_source_lod_into_t create_attribute_source_lod_into (const attribute_t &attr, const attributes_t &attributes)
{
  for (int i = 0; i < int(attributes.attributes.size()); i++)
  {
    if (is_attribute_names_equal(attr, attributes.attributes[i]))
    {
      attribute_source_lod_into_t ret;
      ret.format.first = attributes.attributes[i].format;
      ret.format.second = attributes.attributes[i].components;
      ret.source_index = i;
      return ret;
    }
  }
  attribute_source_lod_into_t ret;
  ret.source_index = -1;
  return ret;
}

attribute_lod_mapping_t attributes_configs_t::get_lod_attribute_mapping(const type_t point_type, const attributes_id_t &target_id, const attributes_id_t *begin, const attributes_id_t *end)
{
  (void)point_type;
  assert(begin < end);
#ifndef NDEBUG
  assert(attributes_ids_increase(begin, end));
#endif
  std::unique_lock<std::mutex> lock(_mutex);
  const auto &target = _attributes_configs[target_id.data].attributes;
  attribute_lod_mapping_t ret;
  ret.destination_id = target_id;
  ret.destination.reserve(target.attributes.size());
  ret.source.reserve(end - begin);
  for (auto &attr : target.attributes)
  {
    ret.destination.emplace_back(attr.format, attr.components);
  }
  for (auto it = begin; it != end; ++it)
  {
    if (std::find_if(ret.source.begin(), ret.source.end(), [it](const attribute_lod_info_t &a) { return it->data == a.source_id.data; }) != ret.source.end())
    {
      continue;
    }
    ret.source.emplace_back();
    auto &source = ret.source.back();
    source.source_id = *it;
    source.source_attributes.reserve(target.attributes.size());
    auto &source_attr = this->_attributes_configs[source.source_id.data].attributes;

    for (auto &target_attr : target.attributes)
    {
      source.source_attributes.push_back(create_attribute_source_lod_into(target_attr, source_attr));
    }
  }
  return ret;
}

std::vector<std::pair<type_t, components_t>> attributes_configs_t::get_format_components(attributes_id_t id)
{
  assert(id.data < _attributes_configs.size());
  std::unique_lock<std::mutex> lock(_mutex);
  auto &attrib_config = _attributes_configs[id.data];
  std::vector<std::pair<type_t, components_t>> ret;
  ret.reserve(attrib_config.attributes.attributes.size());
  for (auto &attrib : attrib_config.attributes.attributes)
  {
    ret.emplace_back(attrib.format, attrib.components);
  }
  return ret;
}

std::pair<type_t, components_t> attributes_configs_t::get_point_format(attributes_id_t id)
{
  assert(id.data < _attributes_configs.size());
  std::unique_lock<std::mutex> lock(_mutex);
  auto &attrib = _attributes_configs[id.data].attributes.attributes[0];
  return { attrib.format, attrib.components };
}
}}
