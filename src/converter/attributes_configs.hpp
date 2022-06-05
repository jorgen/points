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
#ifndef ATTRIBUTES_CONFIGS_HPP
#define ATTRIBUTES_CONFIGS_HPP

#include <memory>
#include <vector>
#include <mutex>

#include <conversion_types.hpp>

namespace points
{
namespace converter
{

struct attributes_extra_info_t
{
  bool is_accumulative;
};

struct attribute_source_lod_into_t
{
  int index;
  std::pair<type_t, components_t> format;
};

struct attribute_lod_info_t
{
  attributes_id_t id;
  std::vector<attribute_source_lod_into_t> source_attributes;
};
struct attribute_lod_mapping_t
{
  attributes_id_t destination_id;
  std::vector<std::pair<type_t, components_t>> destination;
  std::vector<attribute_lod_info_t> source;
  const attribute_lod_info_t &get_source_mapping(attributes_id_t id) const
  {
    auto it = std::find_if(source.begin(), source.end(), [id](const attribute_lod_info_t &info)
    {
      return info.id.data == id.data;
    });
    assert(it != source.end());
    return *it;
  }
};

struct attribute_config_t
{
  attributes_t attributes;
  std::vector<attributes_extra_info_t> extra_info;
  std::vector<attributes_id_t> child_attributes;
  attribute_lod_mapping_t attribute_lod_mapping;
};

class attributes_configs_t
{
public:
  attributes_configs_t(const tree_global_state_t &global_state);

  attributes_id_t get_attribute_config_index(attributes_t &&attr);

  const attributes_t &get(attributes_id_t id);

  attribute_lod_mapping_t get_lod_attribute_mapping(const type_t point_type, const attributes_id_t *begin, const attributes_id_t *end);
  std::vector<std::pair<type_t, components_t>> get_format_components(attributes_id_t id);
private:
  const tree_global_state_t &_global_state;
  std::mutex _mutex;
  std::vector<attribute_config_t> _attributes_configs;
};

}}
#endif // ATTRIBUTES_CONFIGS_HPP
