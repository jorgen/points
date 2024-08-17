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
#pragma once

#include <attributes_configs.hpp>
#include <glm_include.hpp>
#include <points/render/aabb.h>
#include <tree_handler.hpp>

namespace points
{
namespace converter
{

struct node_id_t
{
  tree_id_t tree_id;
  uint16_t level;
  uint16_t index;
};

static_assert(sizeof(node_id_t) == sizeof(uint64_t), "size mismatch");

struct tree_walker_data_t
{
  int lod;
  node_id_t node_id;
  glm::dvec3 center;
  offset_in_subset_t offset_in_subset;
  point_count_t point_count;
  storage_location_t locations[4];
};

struct tree_walker_nodes_t
{
  std::vector<tree_walker_data_t> point_subsets;
};

class attribute_index_map_t
{
public:
  attribute_index_map_t(const attributes_configs_t &m_attributes_configs, std::vector<std::string> attribute_names)
    : m_attributes_configs(m_attributes_configs)
    , m_attribute_names(std::move(attribute_names))
  {
  }

  int get_attribute_count() const
  {
    return int(m_attribute_names.size());
  }

  int get_index(attributes_id_t id, int attribute_name_index)
  {
    assert(attribute_name_index < int(m_attribute_names.size()));
    auto it = m_map.find(id);
    if (it != m_map.end())
    {
      auto ret = it->second[attribute_name_index];
      if (ret >= 0)
      {
        return ret;
      }
    }
    auto &value = it != m_map.end() ? it->second : m_map[id];
    value.resize(m_attribute_names.size(), -1);
    auto index = m_attributes_configs.get_attribute_index(id, m_attribute_names[attribute_name_index]);
    value[attribute_name_index] = index;
    return index;
  }

private:
  struct index_pair_t
  {
    int attribute_name_index;
    int index;
  };
  struct hash
  {
    using is_avalanching = void;
    auto operator()(attributes_id_t id) const noexcept -> uint64_t
    {
      uint32_t data;
      static_assert(sizeof(data) == sizeof(id), "size mismatch");
      memcpy(&data, &id, sizeof(data));
      return ankerl::unordered_dense::detail::wyhash::hash(data);
    }
  };
  const attributes_configs_t &m_attributes_configs;
  std::vector<std::string> m_attribute_names;
  ankerl::unordered_dense::map<attributes_id_t, std::vector<int>, hash> m_map;
};

class frustum_tree_walker_t
{
public:
  frustum_tree_walker_t(glm::dmat4 view_perspective, int depth, std::vector<std::string> attribute_names);
  bool done();
  void wait_done();

  glm::dmat4 m_view_perspective;
  int m_depth;
  std::vector<std::string> m_attribute_names;
  std::mutex m_mutex;
  std::condition_variable m_wait;
  tree_walker_nodes_t m_new_nodes;
  bool m_done;
};

void tree_walk_in_handler_thread(tree_handler_t &tree_handler, tree_registry_t &tree_registry, attribute_index_map_t &attribute_index_map, frustum_tree_walker_t &walker);

} // namespace converter
} // namespace points
