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
#include <mutex>
#include <vector>

#include <conversion_types.hpp>

namespace points
{
namespace converter
{

struct attribute_source_lod_into_t
{
  int source_index;
  point_format_t source_format;
};

struct attribute_lod_info_t
{
  attributes_id_t source_id;
  std::vector<attribute_source_lod_into_t> source_attributes;
};

struct attribute_lod_mapping_t
{
  attributes_id_t destination_id;
  std::vector<point_format_t> destination;
  std::vector<attribute_lod_info_t> source;
  const attribute_lod_info_t &get_source_mapping(attributes_id_t id) const
  {
    auto it = std::find_if(source.begin(), source.end(), [id](const attribute_lod_info_t &info) { return info.source_id.data == id.data; });
    assert(it != source.end());
    return *it;
  }
};

struct attribute_config_t
{
  attributes_t attributes;
};

struct serialized_attributes_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size = 0;
};

struct attribute_index_t
{
  int index;
  point_format_t format;
};

class attributes_configs_t
{
public:
  attributes_id_t get_attribute_config_index(attributes_t &&attr);
  attribute_lod_mapping_t get_lod_attribute_mapping(int lod, const attributes_id_t *begin, const attributes_id_t *end);
  attribute_lod_mapping_t get_lod_attribute_mapping(const type_t point_type, const attributes_id_t &target_id, const attributes_id_t *begin, const attributes_id_t *end);

  const attributes_t &get(attributes_id_t id);

  std::vector<point_format_t> get_format_components(attributes_id_t id);
  point_format_t get_point_format(attributes_id_t id);

  attribute_index_t get_attribute_index(attributes_id_t id, const std::string &name) const;

  serialized_attributes_t serialize() const;
  [[nodiscard]] points::error_t deserialize(const std::unique_ptr<uint8_t[]> &data, uint32_t size);

  uint32_t attrib_name_registry_count() const;
  uint32_t attrib_name_registry_get(uint32_t index, char *name, uint32_t buffer_size) const;

private:
  mutable std::mutex _mutex;
  std::vector<attribute_config_t> _attributes_configs;
  std::vector<std::string> _attribute_name_registry;
};

} // namespace converter
} // namespace points
#endif // ATTRIBUTES_CONFIGS_HPP
