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

#include "fixed_size_vector.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include <algorithm>
#include <mutex>

namespace points::converter
{

static bool compare_attribute(const attribute_t &a, const attribute_t &b)
{
  if (a.name_size != b.name_size)
    return false;
  if (a.type != b.type)
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

bool attributes_name_in_registry(const attribute_t &attribute, const std::vector<std::string> &registry)
{
  for (auto &name : registry)
  {
    if (attribute.name_size == uint32_t(name.size()) && memcmp(attribute.name, name.data(), name.size()) == 0)
    {
      return true;
    }
  }
  return false;
}

static void insert_new_names(const attributes_t &source, std::vector<std::string> &target)
{
  for (auto &attrib : source.attributes)
  {
    if (!attributes_name_in_registry(attrib, target))
    {
      target.emplace_back(attrib.name, attrib.name_size);
    }
  }
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
  insert_new_names(new_config.attributes, _attribute_name_registry);
  return {uint32_t(ret)};
}

static bool contains_attribute(const attributes_t &attributes, const attribute_t &attribute)
{
  for (auto &to_check_attrib : attributes.attributes)
  {
    if (to_check_attrib.name_size == attribute.name_size && memcmp(to_check_attrib.name, attribute.name, attribute.name_size) == 0)
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

  auto lod_type = morton_type_from_lod(lod);
  attributes_t target;
  {
    std::unique_lock<std::mutex> lock(_mutex);
    attributes_copy(_attributes_configs[attrib_begin->data].attributes, target);
    auto it = attrib_begin;
    while (++it != attrib_end)
    {
      add_missing_attributes(_attributes_configs[it->data].attributes, target);
    }
    target.attributes.front().type = lod_type;
    target.attributes.front().components = components_1;
  }
  auto id = get_attribute_config_index(std::move(target));
  return get_lod_attribute_mapping(lod_type, id, attrib_begin, attrib_end);
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
  while (begin < end)
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

attribute_source_lod_into_t create_attribute_source_lod_into(const attribute_t &attr, const attributes_t &attributes)
{
  for (int i = 0; i < int(attributes.attributes.size()); i++)
  {
    if (is_attribute_names_equal(attr, attributes.attributes[i]))
    {
      const point_format_t source_format{attributes.attributes[i].type, attributes.attributes[i].components};
      return {i, source_format};
    }
  }
  return {-1, {}};
}

attribute_lod_mapping_t attributes_configs_t::get_lod_attribute_mapping(const type_t point_type, const attributes_id_t &target_id, const attributes_id_t *begin, const attributes_id_t *end) const
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
    ret.destination.emplace_back(attr.type, attr.components);
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

std::vector<point_format_t> attributes_configs_t::get_format_components(attributes_id_t id)
{
  assert(id.data < _attributes_configs.size());
  std::unique_lock<std::mutex> lock(_mutex);
  auto &attrib_config = _attributes_configs[id.data];
  std::vector<point_format_t> ret;
  ret.reserve(attrib_config.attributes.attributes.size());
  for (auto &attrib : attrib_config.attributes.attributes)
  {
    ret.emplace_back(attrib.type, attrib.components);
  }
  return ret;
}

point_format_t attributes_configs_t::get_point_format(attributes_id_t id)
{
  assert(id.data < _attributes_configs.size());
  std::unique_lock<std::mutex> lock(_mutex);
  auto &attrib = _attributes_configs[id.data].attributes.attributes[0];
  return {attrib.type, attrib.components};
}

attribute_index_t attributes_configs_t::get_attribute_index(attributes_id_t id, const std::string &name) const
{
  std::unique_lock<std::mutex> lock(_mutex);
  if (id.data >= _attributes_configs.size())
    return {-1, {}};
  if (_attributes_configs[id.data].attributes.attributes.empty())
    return {-1, {}};
  for (int i = 0; i < int(_attributes_configs[id.data].attributes.attributes.size()); i++)
  {
    auto &attrib = _attributes_configs[id.data].attributes.attributes[i];
    if (attrib.name_size == name.size() && memcmp(attrib.name, name.data(), name.size()) == 0)
    {
      return {i, {attrib.type, attrib.components}};
    }
  }
  return {-1, {}};
}

serialized_attributes_t attributes_configs_t::serialize() const
{
  auto count = uint32_t(_attributes_configs.size());

  auto size = uint32_t(0);
  size += sizeof(count);

  for (auto &attrib : _attributes_configs)
  {
    size += uint32_t(attrib.attributes.attributes.size());
    for (auto &attr : attrib.attributes.attributes)
    {
      size += sizeof(attr.type) + sizeof(attr.components) + sizeof(attr.name_size) + attr.name_size;
    }
  }
  auto ret = serialized_attributes_t();
  ret.size = size;
  ret.data = std::make_shared<uint8_t[]>(size);
  auto data = ret.data.get();

  memcpy(data, &count, sizeof(count));
  data += sizeof(count);
  for (auto &attrib : _attributes_configs)
  {
    auto attrib_count = uint32_t(attrib.attributes.attributes.size());
    memcpy(data, &attrib_count, sizeof(attrib_count));
    data += sizeof(attrib_count);
    for (auto &attr : attrib.attributes.attributes)
    {
      memcpy(data, &attr.type, sizeof(attr.type));
      data += sizeof(attr.type);
      memcpy(data, &attr.components, sizeof(attr.components));
      data += sizeof(attr.components);
      memcpy(data, &attr.name_size, sizeof(attr.name_size));
      data += sizeof(attr.name_size);
      memcpy(data, attr.name, attr.name_size);
      data += attr.name_size;
    }
  }
  return ret;
}

points::error_t attributes_configs_t::deserialize(const std::unique_ptr<uint8_t[]> &data, uint32_t size)
{
  auto input_bytes = data.get();
  auto end = input_bytes + size;
  uint32_t count;

  if (input_bytes + sizeof(uint32_t) > end)
    return {1, "Invalid input size for count"};

  memcpy(&count, input_bytes, sizeof(count));
  input_bytes += sizeof(uint32_t);

  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t attrib_count;
    if (input_bytes + sizeof(uint32_t) > end)
      return {2, "Invalid input size for attribute count"};

    memcpy(&attrib_count, input_bytes, sizeof(attrib_count));
    input_bytes += sizeof(uint32_t);

    attributes_t attributes;
    for (uint32_t j = 0; j < attrib_count; j++)
    {
      if (input_bytes + sizeof(type_t) + sizeof(components_t) + sizeof(uint32_t) > end)
        return {3, "Invalid input size for attribute details"};

      type_t format;
      components_t components;
      uint32_t name_size;

      memcpy(&format, input_bytes, sizeof(format));
      input_bytes += sizeof(format);

      memcpy(&components, input_bytes, sizeof(components));
      input_bytes += sizeof(components);

      memcpy(&name_size, input_bytes, sizeof(name_size));
      input_bytes += sizeof(name_size);

      if (input_bytes + name_size > end)
        return {4, "Invalid input size for attribute name"};

      auto name = std::make_unique<char[]>(name_size + 1);
      memcpy(name.get(), input_bytes, name_size);
      name[name_size] = '\0';
      input_bytes += name_size;

      attributes.attributes.emplace_back(name.get(), name_size, format, components);
      attributes.attribute_names.push_back(std::move(name));
    }
    get_attribute_config_index(std::move(attributes));
  }
  return {};
}
uint32_t attributes_configs_t::attrib_name_registry_count() const
{
  std::unique_lock<std::mutex> lock(_mutex);
  return uint32_t(_attribute_name_registry.size());
}
uint32_t attributes_configs_t::attrib_name_registry_get(uint32_t index, char *name, uint32_t buffer_size) const
{
  std::unique_lock<std::mutex> lock(_mutex);
  if (index >= _attribute_name_registry.size())
    return 0;
  auto &attrib = _attribute_name_registry[index];
  auto size = uint32_t(attrib.size()) + 1;
  if (size > buffer_size)
    size = buffer_size;
  memcpy(name, attrib.data(), size - 1);
  name[size - 1] = 0;
  return size - 1;
}

} // namespace points::converter
