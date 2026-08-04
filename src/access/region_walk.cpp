/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/

#include "region_walk.hpp"

#include "morton_tree_coordinate_transform.hpp"

#include <atomic>

namespace dew::access
{

aabb_t aabb_child(const aabb_t &parent, int child_index)
{
  aabb_t out = parent;
  for (int i = 0; i < 3; i++)
  {
    const double mid = (parent.min[i] + parent.max[i]) * 0.5;
    if (child_index & (1 << i))
      out.min[i] = mid;
    else
      out.max[i] = mid;
  }
  return out;
}

namespace
{

struct pending_node_t
{
  const tree_t *tree;
  int skip;
  aabb_t cell;
  bool fully_inside; // the cell lies wholly inside the query box -> children inherit it, no re-test
};

aabb_t cell_from_morton(const tree_config_t &config, const morton::morton192_t &min, const morton::morton192_t &max)
{
  double lo[3];
  double hi[3];
  convert_morton_to_pos(config.scale, config.offset, min, lo);
  convert_morton_to_pos(config.scale, config.offset, max, hi);
  return aabb_t{{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}};
}

bool tree_resident(const tree_registry_t &registry, tree_id_t id)
{
  if (id.data >= registry.tree_id_initialized.size())
    return false;
  return std::atomic_ref<uint8_t>(const_cast<uint8_t &>(registry.tree_id_initialized[id.data])).load(std::memory_order_acquire) != 0;
}

// Emit every storage unit held at this node, honouring the LOD rule: a full-resolution query takes
// only leaf data, anything else takes only the sampled LOD unit.
void emit_node(const tree_registry_t &registry, const tree_t *tree, int level, int skip, const aabb_t &cell, bool fully_inside, const region_query_t &query, region_result_t &out)
{
  const auto &collection = tree->data[level][size_t(skip)];
  if (collection.data.empty())
    return;

  const auto tight = cell_from_morton(registry.tree_config, collection.min, collection.max);
  if (!query.whole_dataset && !aabb_overlaps(tight, query.box))
    return;

  const int lod = morton::morton_tree_level_to_lod(tree->magnitude, level);
  const uint8_t child_mask = tree->nodes[level][size_t(skip)];

  for (const auto &subset : collection.data)
  {
    const bool leaf_data = input_data_id_is_leaf(subset.input_id);
    // full resolution wants source points only; every other mode wants the sampled copy only.
    if (query.lod_mode == lod_mode_t::full)
    {
      if (!leaf_data)
        continue;
    }
    else if (child_mask != 0 && leaf_data)
    {
      // An interior node that also carries leaf data: at a coarse LOD the sampled unit represents it.
      continue;
    }

    region_node_t node;
    node.tree_id = tree->id;
    node.level = uint16_t(level);
    node.index = tree->node_ids[level][size_t(skip)];
    node.lod = lod;
    node.input_id = subset.input_id;
    node.attributes_id = tree->storage_map.attribute_id(subset.input_id);
    node.offset_in_subset = subset.offset;
    node.point_count = subset.count;
    node.cell = cell;
    node.tight = tight;
    node.child_mask = child_mask;
    node.is_leaf = child_mask == 0;
    node.is_lod = !leaf_data;
    node.fully_inside = fully_inside || aabb_contains(query.box, tight);
    out.total_points += subset.count.data;
    out.nodes.push_back(node);
  }
}

} // namespace

void region_walk(const tree_registry_t &registry, const region_query_t &query, region_result_t &out)
{
  out.nodes.clear();
  out.trees_to_load.clear();
  out.total_points = 0;

  if (registry.data.empty())
    return;
  if (!tree_resident(registry, registry.root))
  {
    out.trees_to_load.push_back(registry.root);
    return;
  }
  const tree_t *root = registry.get(registry.root);
  if (!root || root->data[0].empty() || root->data[0][0].data.empty())
    return;

  const aabb_t root_cell = cell_from_morton(registry.tree_config, root->morton_min, root->morton_max);
  if (!query.whole_dataset && !aabb_overlaps(root_cell, query.box))
    return;

  std::vector<pending_node_t> current;
  std::vector<pending_node_t> next;
  current.push_back({root, 0, root_cell, query.whole_dataset || aabb_contains(query.box, root_cell)});

  // A tree is five levels deep and hops to a sub-tree below that, so the absolute depth is unbounded
  // in principle; 40 matches the renderer's cap and is far beyond any real dataset.
  constexpr int max_depth = 40;
  for (int depth = 0; depth < max_depth && !current.empty(); depth++)
  {
    const int level = depth % 5;
    next.clear();

    for (const auto &pending : current)
    {
      const tree_t *tree = pending.tree;
      const uint8_t children = tree->nodes[level][size_t(pending.skip)];
      const int lod = morton::morton_tree_level_to_lod(tree->magnitude, level);

      bool descend = children != 0;
      if (descend && query.lod_mode == lod_mode_t::level && lod <= query.lod)
        descend = false;
      if (descend && query.lod_mode == lod_mode_t::point_budget && out.total_points >= query.max_points)
        descend = false;

      if (!descend)
      {
        emit_node(registry, tree, level, pending.skip, pending.cell, pending.fully_inside, query, out);
        continue;
      }

      // Interior node: recurse into the octants that exist and overlap the box.
      //
      // child_count indexes skips[]/sub_trees[] by SET-BIT ORDINAL, so it must advance for every set
      // bit -- including octants skipped for being outside the box or for living in a sub-tree that is
      // not resident. Failing to advance it silently misroutes every later sibling to the wrong node.
      int child_count = 0;
      uint8_t bits = children;
      for (int i = 0; i < 8; i++, bits >>= 1)
      {
        if (!(bits & 1))
          continue;
        const int this_child = child_count++;

        const aabb_t child_cell = aabb_child(pending.cell, i);
        if (!query.whole_dataset && !aabb_overlaps(child_cell, query.box))
          continue;

        const tree_t *child_tree = tree;
        int child_skip = tree->skips[level][size_t(pending.skip)] + this_child;
        if (level == 4)
        {
          const auto sub_tree_id = tree->sub_trees[size_t(tree->skips[4][size_t(pending.skip)] + this_child)];
          if (!tree_resident(registry, sub_tree_id))
          {
            out.trees_to_load.push_back(sub_tree_id);
            continue;
          }
          child_tree = registry.get(sub_tree_id);
          if (!child_tree)
            continue;
          child_skip = 0;
        }
        const bool inside = pending.fully_inside || aabb_contains(query.box, child_cell);
        next.push_back({child_tree, child_skip, child_cell, inside});
      }
    }
    current.swap(next);
  }
}

} // namespace dew::access
