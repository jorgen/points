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
#include "frustum_tree_walker.hpp"

#include "attributes_configs.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include <fmt/printf.h>
#include <points/render/aabb.h>

namespace points::converter
{
inline bool frustum_contains_aabb2(const glm::dmat4 &view_perspective, const render::aabb_t &aabb)
{
  glm::dvec4 min_vec(aabb.min[0], aabb.min[1], aabb.min[2], 1.0);
  glm::dvec4 max_vec(aabb.max[0], aabb.max[1], aabb.max[2], 1.0);

  glm::dvec4 projected_min = view_perspective * min_vec;
  glm::dvec4 projected_max = view_perspective * max_vec;

  if (projected_min.x < projected_max.x)
  {
    if (projected_max.x < -projected_max.w || projected_min.x > projected_min.w)
    {
      return false;
    }
  }
  else
  {
    if (projected_min.x < -projected_min.w || projected_max.x > projected_max.w)
    {
      return false;
    }
  }
  if (projected_min.y < projected_max.y)
  {
    if (projected_max.y < -projected_max.w || projected_min.y > projected_min.w)
    {
      return false;
    }
  }
  else
  {
    if (projected_min.y < -projected_min.w || projected_max.y > projected_max.w)
    {
      return false;
    }
  }
  if (projected_min.z < projected_max.z)
  {
    if (projected_max.z < -projected_max.w || projected_min.z > projected_min.w)
    {
      return false;
    }
  }
  else
  {
    if (projected_min.z < -projected_min.w || projected_max.z > projected_max.w)
    {
      return false;
    }
  }

  return true;
}
frustum_tree_walker_t::frustum_tree_walker_t(const glm::dmat4 view_perspective, const attributes_configs_t &attributes_configs, std::vector<std::string> attribute_names)
  : m_view_perspective(view_perspective)
  , m_attribute_index_map(attributes_configs, std::move(attribute_names))
  , m_done(false)
{
}

static void walk_tree_l(const tree_t &tree, const glm::dmat4 &view_perspective, attribute_index_map_t &attribute_index_map, int level, int index, const render::aabb_t &aabb, tree_walker_nodes_t &nodes)
{
  auto node = tree.nodes[level][index];
  nodes.morton_nodes[level].emplace_back(tree.node_ids[level][index]);
  auto &point_collection = tree.data[level][index];
  nodes.point_subsets[level].emplace_back();
  auto &subset_data = nodes.point_subsets[level];
  for (const auto &point_subset : point_collection.data)
  {
    auto &subset = subset_data.emplace_back();
    subset.reserve(attribute_index_map.attribute_count());
    auto input_info = tree.storage_map.info(point_subset.input_id);
    for (int i = 0; i < attribute_index_map.attribute_count(); i++)
    {
      auto attribute_index = attribute_index_map.get_index(input_info.first, i);
      subset.emplace_back(point_subset.offset, point_subset.count, input_info.second[attribute_index]);
    }
  }
  if (!node || level == 4)
    return;
  int children = 0;
  for (int i = 0; i < 8; i++)
  {
    if (node & 1 << i)
    {
      walk_tree_l(tree, view_perspective, attribute_index_map, level + 1, tree.skips[level][index] + children, aabb, nodes);
      children++;
    }
  }
}

void frustum_tree_walker_t::walk_tree(tree_registry_t tree_cache, tree_id_t tree_root)
{
  (void)tree_root;
  auto root_tree = tree_cache.get(tree_root);
  convert_morton_to_pos(tree_cache.tree_config.scale, tree_cache.tree_config.offset, root_tree->morton_min, m_tree_aabb.min);
  convert_morton_to_pos(tree_cache.tree_config.scale, tree_cache.tree_config.offset, root_tree->morton_max, m_tree_aabb.max);

  if (root_tree->nodes[0].size() && frustum_contains_aabb2(m_view_perspective, m_tree_aabb))
  {
    m_new_nodes.id.data = root_tree->id.data;
    m_new_nodes.min_morton = root_tree->morton_min;
    m_new_nodes.level = root_tree->magnitude;
    walk_tree_l(*root_tree, m_view_perspective, m_attribute_index_map, 0, 0, m_tree_aabb, m_new_nodes);
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  m_done = true;
}

bool frustum_tree_walker_t::done()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_done;
}
attribute_index_map_t::attribute_index_map_t(const attributes_configs_t &attributes_configs, std::vector<std::string> attribute_names)
  : m_attributes_configs(attributes_configs)
  , m_attribute_names(std::move(attribute_names))
{
}

int attribute_index_map_t::get_index(attributes_id_t id, int attribute_name_index)
{
  assert(attribute_name_index < int(m_attribute_names.size()));
  auto it = m_map.find(id);
  if (it == m_map.end())
  {
    return -1;
  }
  auto ret = it->second[attribute_name_index];
  if (ret >= 0)
  {
    return ret;
  }
  it->second[attribute_name_index] = m_attributes_configs.get_attribute_index(id, m_attribute_names[attribute_name_index]);
  return it->second[attribute_name_index];
}

} // namespace points::converter
