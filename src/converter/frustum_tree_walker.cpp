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
#include "frustum.hpp"
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

// static node_id_t create_node_id(tree_id_t tree_id, int level, int index)
//{
//   return {tree_id, uint16_t(level), uint16_t(index)};
// }

struct tree_walker_possible_nodes_t
{
  tree_walker_possible_nodes_t() = default;
  tree_walker_possible_nodes_t(tree_t *tree, node_id_t parent, uint16_t skip, node_aabb_t aabbs, bool is_completely_inside_frustum)
    : tree(tree)
    , parent(parent)
    , skip(skip)
    , aabbs(aabbs)
    , is_completely_inside_frustum(is_completely_inside_frustum)
  {
  }
  tree_t *tree;
  node_id_t parent;
  uint16_t skip;
  node_aabb_t aabbs;
  bool is_completely_inside_frustum;
};

static node_aabb_t make_aabb_from_child_index(const node_aabb_t &parent, int child_index)
{
  node_aabb_t aabb;
  aabb.min = parent.min;
  aabb.max = parent.max;
  for (int i = 0; i < 3; i++)
  {
    if (child_index & (1 << i))
      aabb.min[i] = (aabb.min[i] + aabb.max[i]) / 2;
    else
      aabb.max[i] = (aabb.min[i] + aabb.max[i]) / 2;
  }
  return aabb;
}

static void walk_tree(tree_handler_t &tree_handler, tree_registry_t &tree_registry, attribute_index_map_t &attribute_index_map, tree_id_t tree_id, int depth_left, frustum_tree_walker_t &walker)
{
  (void)attribute_index_map;
  (void)walker;
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
  double max[3];
  convert_morton_to_pos(tree_registry.tree_config.scale, tree_registry.tree_config.offset, tree->morton_min, min);
  convert_morton_to_pos(tree_registry.tree_config.scale, tree_registry.tree_config.offset, tree->morton_max, max);

  node_aabb_t aabb = {glm::dvec3(min[0], min[1], min[2]), glm::dvec3(max[0], max[1], max[2])};

  auto tree_lod = morton::morton_tree_level_to_lod(tree->magnitude, 0); // we skip the + 1, since we want the half
  double aabb_width_half = double(uint64_t(1) << tree_lod) * tree_registry.tree_config.scale;
  glm::dvec3 center = {min[0] + aabb_width_half, min[1] + aabb_width_half, min[2] + aabb_width_half};

  node_id_t empty_node_id = {};
  alternating_possible_nodes[current_buffer_index].emplace_back(tree, empty_node_id, 0, aabb, false);
  render::frustum_t frustum;
  frustum.update(walker.m_view_perspective);
  int lod = tree->magnitude * 5 + 5;
  for (int depth = 0; depth < depth_left; depth++, current_buffer_index = !current_buffer_index, lod--)
  {
    int current_depth_in_tree = depth % 5;
    for (auto &possible_nodes : alternating_possible_nodes[current_buffer_index])
    {
      auto hit_test = possible_nodes.is_completely_inside_frustum ? render::frustum_intersection_t::inside : frustum.test_aabb(possible_nodes.aabbs.min, possible_nodes.aabbs.max);
      if (hit_test == render::frustum_intersection_t::outside)
        continue;
      auto node_name = possible_nodes.tree->node_ids[current_depth_in_tree][possible_nodes.skip];
      node_id_t node_id = {tree->id, uint16_t(current_depth_in_tree), node_name};
      auto &points_collection = tree->data[current_depth_in_tree][possible_nodes.skip];
      for (auto &points : points_collection.data)
      {
        auto attr_id = tree->storage_map.attribute_id(points.input_id);
        int attrib_indexes[] = {-2, -2, -2, -2};
        bool valid = true;
        for (int i = 0; i < 4 && i < attribute_index_map.get_attribute_count(); i++)
        {
          auto index = attribute_index_map.get_index(attr_id, i);
          attrib_indexes[i] = index;
          if (index == -1)
          {
            valid = false;
            break;
          }
        }
        if (!valid)
        {
          continue;
        }
        auto &to_add = walker.m_new_nodes.point_subsets.emplace_back();
        memset(to_add.locations, 0, sizeof(to_add.locations));
        to_add.parent = possible_nodes.parent;
        to_add.lod = lod;
        to_add.node = node_id;
        to_add.aabb = possible_nodes.aabbs;
        to_add.offset_in_subset = points.offset;
        to_add.point_count = points.count;
        to_add.input_id = points.input_id;
        for (int i = 0; i < 4 && i < attribute_index_map.get_attribute_count(); i++)
        {
          auto location = tree->storage_map.location(points.input_id, attrib_indexes[i]);
          to_add.locations[i] = location;
        }
      }

      auto children = tree->nodes[current_depth_in_tree][possible_nodes.skip];
      int child_count = 0;
      for (int i = 0; i < 8 && children; i++, children >>= 1)
      {
        if (children & 1)
        {
          auto child_aabb = make_aabb_from_child_index(possible_nodes.aabbs, i);
          bool is_completely_inside_frustum = hit_test == render::frustum_intersection_t::inside;
          alternating_possible_nodes[!current_buffer_index].emplace_back(possible_nodes.tree, node_id, tree->skips[current_depth_in_tree][possible_nodes.skip] + child_count, child_aabb, is_completely_inside_frustum);
          child_count++;
        }
      }
    }
  }
}

void tree_walk_in_handler_thread(tree_handler_t &tree_handler, tree_registry_t &tree_registry, attribute_index_map_t &attribute_index_map, frustum_tree_walker_t &walker)
{
  auto root = tree_registry.root;
  walk_tree(tree_handler, tree_registry, attribute_index_map, root, walker.m_depth, walker);

  std::unique_lock<std::mutex> lock(walker.m_mutex);
  walker.m_done = true;
  walker.m_wait.notify_all();
}

} // namespace points::converter
