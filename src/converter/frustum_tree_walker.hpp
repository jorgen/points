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

#include <points/render/aabb.h>
#include <tree_handler.hpp>

#include <glm_include.hpp>

namespace points
{
namespace converter
{

struct tree_walker_data_t
{
  offset_in_subset_t offset_in_buffer;
  point_count_t point_count;
  storage_location_t location;
};

struct tree_walker_nodes_t
{
  morton::morton192_t min_morton;
  tree_id_t id;
  uint16_t level;
  std::vector<uint16_t> morton_nodes[5];
  std::vector<std::vector<tree_walker_data_t>> point_subsets[5];
};

class attribute_index_map_t
{
public:
  attribute_index_map_t(const attributes_configs_t &m_attributes_configs, std::vector<std::string> attribute_names);
  int attribute_count() const
  {
    return int(m_attribute_names.size());
  }
  int get_index(attributes_id_t, int attribute_name_index);

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
  frustum_tree_walker_t(glm::dmat4 view_perspective, const attributes_configs_t &attributes_configs, std::vector<std::string> attribute_names);
  void walk_tree(const tree_config_t &global_state, tree_registry_t tree_cache, tree_id_t tree_root);

  bool done();

  glm::dmat4 m_view_perspective;
  attribute_index_map_t m_attribute_index_map;
  render::aabb_t m_tree_aabb;
  std::mutex m_mutex;
  tree_walker_nodes_t m_new_nodes;
  bool m_done;
};

} // namespace converter
} // namespace points
