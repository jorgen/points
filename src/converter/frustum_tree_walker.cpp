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

#include <points/render/aabb.h>
#include "morton_tree_coordinate_transform.hpp"
#include <fmt/printf.h>

namespace points
{
namespace converter
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
frustum_tree_walker_t::frustum_tree_walker_t(glm::dmat4 view_perspective)
    : m_view_perspective(view_perspective)
    , m_done(false)
{

}

static void walk_tree_l(const tree_global_state_t &global_state, const tree_t &tree, const glm::dmat4 &view_perspective, int level, int index, const render::aabb_t &aabb, tree_walker_nodes_t &nodes)
{
  auto node = tree.nodes[level][index];
  nodes.morton_nodes[level].emplace_back(tree.node_ids[level][index]);
  auto &point_collection = tree.data[level][index];
  nodes.point_subsets[level].emplace_back();
  auto &subset_data = nodes.point_subsets[level].back();
  subset_data.data.insert(subset_data.data.end(), point_collection.data.begin(), point_collection.data.end());
  if (!node || level == 4)
    return;
  int children = 0;
  for (int i = 0; i < 8; i++)
  {
    if (node & 1 << i)
    {
      walk_tree_l(global_state, tree, view_perspective, level + 1, tree.skips[level][index] + children, aabb, nodes);
      children++;
    }
  }
}

void frustum_tree_walker_t::walk_tree(const tree_global_state_t &global_state, tree_cache_t tree_cache, tree_id_t tree_root)
{
  (void) tree_root;
  auto root_tree = tree_cache.get(tree_root);
  convert_morton_to_pos(global_state.scale, global_state.offset, root_tree->morton_min, m_tree_aabb.min);
  convert_morton_to_pos(global_state.scale, global_state.offset, root_tree->morton_max, m_tree_aabb.max);


  if (root_tree->nodes[0].size() && frustum_contains_aabb2(m_view_perspective, m_tree_aabb))
  {
    m_new_nodes.id.data = root_tree->id.data;
    m_new_nodes.min_morton = root_tree->morton_min;
    m_new_nodes.level = root_tree->magnitude;
    walk_tree_l(global_state, *root_tree, m_view_perspective, 0, 0, m_tree_aabb, m_new_nodes);
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  m_done = true;
}

bool frustum_tree_walker_t::done()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_done;
}
}
}
