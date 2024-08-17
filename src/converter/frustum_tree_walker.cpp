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
frustum_tree_walker_t::frustum_tree_walker_t(const glm::dmat4 view_perspective, int depth, std::vector<std::string> attribute_names)
  : m_view_perspective(view_perspective)
  , m_depth(depth)
  , m_attribute_names(std::move(attribute_names))
  , m_done(false)
{
}

bool frustum_tree_walker_t::done()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_done;
}

void frustum_tree_walker_t::wait_done()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_wait.wait(lock, [this] { return m_done; });
}

static node_id_t create_node_id(tree_id_t tree_id, int level, int index)
{
  return {tree_id, uint16_t(level), uint16_t(index)};
}

struct tree_walker_possible_nodes_t
{
  tree_t *tree;
  std::vector<uint16_t> skips;
  std::vector<glm::dvec3> centers;
  std::vector<bool> is_completly_inside_frustum;
};

static void walk_tree(tree_handler_t &tree_handler, tree_registry_t &tree_registry, attribute_index_map_t &attribute_index_map, tree_id_t tree_id, int depth_left, frustum_tree_walker_t &walker)
{
  if (!tree_handler.tree_initialized(tree_id))
  {
    tree_handler.request_tree(tree_id);
    return;
  }
  auto tree = tree_registry.get(tree_id);
  if (tree->data[0].empty() && tree->data[0][0].data.empty())
    return;

  bool current_buffer_index = false;
  std::vector<tree_walker_possible_nodes_t> alternating_possible_nodes[2];

  double min[3];
  convert_morton_to_pos(tree_registry.tree_config.scale, tree_registry.tree_config.offset, tree->morton_min, min);

  auto tree_lod = morton::morton_tree_level_to_lod(tree->magnitude, 0); // we skip the + 1, since we want the half
  double aabb_width_half = double(uint64_t(1) << tree_lod) * tree_registry.tree_config.scale;
  glm::dvec3 center = {min[0] + aabb_width_half, min[1] + aabb_width_half, min[2] + aabb_width_half};

  alternating_possible_nodes[current_buffer_index].push_back({tree, {0}, {center}, {false}});

  for (int depth = 0; depth < depth_left; depth++)
  {
    int current_depth_in_tree = depth % 5;
    for (auto &possible_nodes : alternating_possible_nodes[current_buffer_index])
    {
      auto &tree = *possible_nodes.tree;
      auto &skips = possible_nodes.skips;
      auto &centers = possible_nodes.centers;
      auto &is_completely_inside_frustum = possible_nodes.is_completly_inside_frustum;
      std::vector<tree_walker_possible_nodes_t> new_possible_nodes;
      assert(skips.size() == centers.size());
      assert(skips.size() == is_completely_inside_frustum.size());
      for (int index_in_possible_nodes = 0; index_in_possible_nodes < possible_nodes.skips.size(); index_in_possible_nodes++)
      {
        auto node = tree.nodes[current_depth_in_tree][skip];
        const auto &node_data = tree.data[current_depth_in_tree][skip];
        const auto &tree_skip = tree.skips[current_depth_in_tree][skip];
        auto &node_id = tree.node_ids[current_depth_in_tree][skip];
        auto &center = centers[skip];
        auto is_completely_inside = is_completely_inside_frustum[skip];
        auto &tree_walker_node = walker.m_new_nodes.point_subsets.emplace_back();
        tree_walker_node.lod = tree.magnitude * 5 + current_depth_in_tree;
      }
    }
  }
}

void tree_walk_in_handler_thread(tree_handler_t &tree_handler, tree_registry_t &tree_registry, attribute_index_map_t &attribute_index_map, frustum_tree_walker_t &walker)
{
  auto root = tree_registry.root;
  assert(walker.m_new_nodes.point_subsets.size() == walker.m_depth);
  walk_tree(tree_handler, tree_registry, attribute_index_map, root, walker.m_depth, walker);

  std::unique_lock<std::mutex> lock(walker.m_mutex);
  walker.m_done = true;
  walker.m_wait.notify_all();
}

} // namespace points::converter
