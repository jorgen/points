/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#include "tree.hpp"
#include <assert.h>
#include <points/converter/default_attribute_names.h>
#include "point_buffer_splitter.hpp"
namespace points
{
namespace converter
{

tree_t &tree_cache_create_root_tree(tree_cache_t &tree_cache)
{
  tree_cache.data.emplace_back();
  tree_cache.data.back().id.data = tree_cache.current_id++;
  return tree_cache.data.back();
}

tree_t &tree_cache_add_tree(tree_cache_t &tree_cache, tree_t *(&parent))
{
  auto id = parent->id;
  tree_cache.data.emplace_back();
  tree_cache.data.back().id.data = tree_cache.current_id++;
  parent = &tree_cache.data[id.data];
  return tree_cache.data.back();
}

tree_id_t tree_initialize(const tree_global_state_t &global_state, tree_cache_t &tree_cache, cache_file_handler_t &cache, const storage_header_t &header)
{
  tree_t &tree = tree_cache_create_root_tree(tree_cache);
  morton::morton192_t mask = morton::morton_xor(header.morton_min, header.morton_max);
  int magnitude = morton::morton_magnitude_from_bit_index(morton::morton_msb(mask));
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(morton::morton_magnitude_to_lod(magnitude));
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(header.morton_min, new_tree_mask);
#ifndef NDEBUG
  double min_pos[3];
  double max_pos[3];
  convert_morton_to_pos(global_state.scale, global_state.offset, tree.morton_min, min_pos);
  convert_morton_to_pos(global_state.scale, global_state.offset, tree.morton_max, max_pos);
  double diff[3];
  diff[0] = int(max_pos[0] - min_pos[0]);
  diff[1] = int(max_pos[1] - min_pos[1]);
  diff[2] = int(max_pos[2] - min_pos[2]);
  assert(diff[0] == diff[1] && diff[0] == diff[2]);
#endif
  tree.magnitude = uint8_t(magnitude);
  tree.nodes[0].push_back(0);
  tree.ids[0].push_back(0);
  tree.skips[0].push_back(int16_t(0));
  tree.data[0].emplace_back();
#ifndef NDEBUG
  tree.mins[0].push_back(tree.morton_min);
#endif

  auto id = tree_add_points(global_state, tree_cache, cache, tree.id, header);
  return id;
}

static void tree_initialize_sub(const tree_t &parent_tree, tree_cache_t &tree_cache, const morton::morton192_t &morton, tree_t &sub_tree)
{
  sub_tree.magnitude = parent_tree.magnitude - 1;
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(sub_tree.magnitude * 5 + 4);
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  sub_tree.morton_min = morton::morton_and(morton, new_tree_mask_inv);
  sub_tree.morton_max = morton::morton_or(morton, new_tree_mask);

  sub_tree.nodes[0].push_back(0);
  sub_tree.ids[0].push_back(0);
  sub_tree.skips[0].push_back(int16_t(0));
  sub_tree.data[0].emplace_back();
#ifndef NDEBUG
  sub_tree.mins[0].push_back(sub_tree.morton_min);
#endif
}

static void tree_initialize_new_parent(const tree_t &some_child, const morton::morton192_t possible_min, const morton::morton192_t possible_max, tree_t &new_parent)
{
  morton::morton192_t new_min = some_child.morton_min < possible_min ? some_child.morton_min : possible_min;
  morton::morton192_t new_max = some_child.morton_max < possible_max ? possible_max : some_child.morton_max;

  int lod = morton::morton_lod(new_min, new_max);
  new_parent.magnitude = uint8_t(morton::morton_magnitude_from_lod(uint8_t(lod)));
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t,3>(lod);
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  new_parent.morton_min = morton::morton_and(new_tree_mask_inv, new_min);
  new_parent.morton_max = morton::morton_or(new_parent.morton_min, new_tree_mask);
  new_parent.nodes[0].push_back(0);
  new_parent.ids[0].push_back(0);
  new_parent.skips[0].push_back(int16_t(0));
  new_parent.data[0].emplace_back();
}

static int sub_tree_count_skips(uint8_t node, int index)
{
  int node_skips = 0;
  for (int i = 0; i < index; i++)
  {
    if (node & uint8_t(1 << i))
      node_skips++;
  }
  return node_skips;
}

static void sub_tree_alloc_children(tree_t &tree, int level, int skip)
{
  assert(skip <= int(tree.skips[level].size()));
  int old_skip = 0;
  if (skip < int(tree.skips[level].size()))
    old_skip = tree.skips[level][skip];
  else if (skip > 0 && skip == int(tree.skips[level].size()))
    old_skip = tree.skips[level].back() + sub_tree_count_skips(tree.nodes[level].back(), 8);

  tree.nodes[level].emplace(tree.nodes[level].begin() + skip);
  tree.skips[level].emplace(tree.skips[level].begin() + skip, old_skip);
  tree.data[level].emplace(tree.data[level].begin() + skip);
#ifndef NDEBUG
  tree.mins[level].emplace(tree.mins[level].begin() + skip);
#endif
}

static void sub_tree_increase_skips(tree_t &tree, int level, int skip)
{
  auto &skips = tree.skips[level];
  auto skips_size = skips.size();
  for (int i = skip + 1; i < int(skips_size); i++)
  {
    skips[i]++;
  }
}

static void sub_tree_split_points_to_children(const tree_global_state_t &state, cache_file_handler_t &cache, points_collection_t &&points, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  for (auto &p : points.data)
  {
    read_points_t p_read(cache, p.input_id, 0);
    assert(p_read.data.size);

    point_buffer_subdivide(state, p_read, p, lod, node_min, children);
  }
  points = points_collection_t();
}

static void sub_tree_insert_points(const tree_global_state_t &state, tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t tree_id, const morton::morton192_t &min, int current_level, int skip, points_collection_t &&points)
{
  auto *tree = tree_cache.get(tree_id);
  assert(tree->id.data < tree_cache.current_id);
  assert(current_level != 0 || tree->morton_min == min);
  assert(tree->mins[current_level][skip] == min);
  assert(skip == 0 || tree->mins[current_level][skip - 1] < tree->mins[current_level][skip]);
  assert(int(tree->mins[current_level].size() -1) == skip || tree->mins[current_level][skip] < tree->mins[current_level][skip + 1]);

  auto &node = tree->nodes[current_level][skip];
  int lod = morton::morton_tree_level_to_lod(tree->magnitude, current_level);
  auto child_mask = morton::morton_get_child_mask(lod, points.min);
  assert(child_mask < 8);
  assert(!(points.min < min));
  assert(!(morton::morton_or(min, morton::morton_mask_create<uint64_t, 3>(lod)) < points.max));
  if (lod > points.min_lod)
  {
    morton::morton192_t new_min = min;
    morton::morton_set_child_mask(lod, child_mask, new_min);
    int sub_skip = tree->skips[current_level][skip] + sub_tree_count_skips(node, child_mask);
    if (node & (1 << child_mask))
    {
      if (current_level == 4)
      {
        sub_tree_insert_points(state, tree_cache, cache, tree->sub_trees[sub_skip], new_min, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_insert_points(state, tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, std::move(points));
      }
      return;
    }
    else if (node)
    {

      node |= uint8_t(1) << child_mask;

      if (current_level == 4)
      {
        auto &sub_tree = tree_cache_add_tree(tree_cache, tree);
        tree->sub_trees.emplace(tree->sub_trees.begin() + sub_skip, sub_tree.id);
        sub_tree_increase_skips(*tree, current_level, skip);
        tree_initialize_sub(*tree,tree_cache, points.min, sub_tree);
        sub_tree_insert_points(state, tree_cache, cache, sub_tree.id, new_min, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_alloc_children(*tree, current_level + 1, sub_skip);
        sub_tree_increase_skips(*tree, current_level, skip);
#ifndef NDEBUG
        tree->mins[current_level + 1][sub_skip] = new_min;
#endif
        sub_tree_insert_points(state, tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, std::move(points));
      }
      return;
    }
  }

  if (node == 0 && tree->data[current_level][skip].point_count + points.point_count <= state.node_limit)
  {
    assert(!(points.min < min));
    points_data_add(tree->data[current_level][skip], std::move(points));
    return;
  }
  assert(points.point_count);
  assert(tree->magnitude > 0 || current_level < 4);
  {
    points_collection_t children_data[8];
    if (!node && tree->data[current_level][skip].point_count)
    {
      sub_tree_split_points_to_children(state, cache, std::move(tree->data[current_level][skip]), lod, min, children_data);
      tree->data[current_level][skip].point_count = 0;
    }
    if (points.point_count)
      sub_tree_split_points_to_children(state, cache, std::move(points), lod, min, children_data);

    int child_count = 0;
    for (int i= 0; i < 8; i++)
    {
      auto &child_data = children_data[i];
      const bool has_this_child = node & (1 << i);
      if (child_data.data.empty())
      {
        if (has_this_child)
          child_count++;
        continue;
      }
      morton::morton192_t new_min = min;
      morton::morton_set_child_mask(lod, uint8_t(i), new_min);
      assert(child_data.min_lod <= lod);

      tree = tree_cache.get(tree_id);
      int sub_skip = tree->skips[current_level][skip] + child_count;

      if (current_level == 4)
      {
        if (!has_this_child)
        {
          node |= uint8_t(1) << i;
          auto &sub_tree = tree_cache_add_tree(tree_cache, tree);
          tree->sub_trees.emplace(tree->sub_trees.begin() + sub_skip, sub_tree.id);
          sub_tree_increase_skips(*tree, current_level, skip);
          tree_initialize_sub(*tree, tree_cache, child_data.min, sub_tree);
          sub_tree_insert_points(state, tree_cache, cache, sub_tree.id, new_min, 0, 0, std::move(child_data));
        }
        else
        {
          tree_t *sub_tree = tree_cache.get(tree->sub_trees[sub_skip]);
          sub_tree_insert_points(state, tree_cache, cache, sub_tree->id, new_min, 0, 0, std::move(child_data));
        }
      }
      else
      {
        if (has_this_child)
        {
          sub_tree_insert_points(state, tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, std::move(child_data));
        }
        else
        {
          node |= uint8_t(1) << i;
          sub_tree_alloc_children(*tree, current_level + 1, sub_skip);
          sub_tree_increase_skips(*tree, current_level, skip);
#ifndef NDEBUG
          tree->mins[current_level + 1][sub_skip] = new_min;
#endif
          sub_tree_insert_points(state, tree_cache, cache, tree_id, new_min, current_level + 1, sub_skip, std::move(child_data));

        }
      }
      child_count++;
    }
  }
}

static void insert_tree_in_tree(tree_cache_t &tree_cache, tree_id_t &parent_id, const tree_id_t &child_id)
{
  tree_t *parent = tree_cache.get(parent_id);
  tree_t *child = tree_cache.get(child_id);
  assert(memcmp(morton::morton_and(morton::morton_negate(morton::morton_xor(parent->morton_min, parent->morton_max)), child->morton_min).data, parent->morton_min.data, sizeof(parent->morton_min)) == 0);
  int current_skip = 0;
  int lod = morton::morton_magnitude_to_lod(parent->magnitude);
  for (int i = 0; i < 4; i++, lod--)
  {
    auto &node = parent->nodes[i][current_skip];
    auto child_mask = morton::morton_get_child_mask(lod, child->morton_min);
    int node_skips = sub_tree_count_skips(node, child_mask);
    if (node & (1 << child_mask))
    {
      current_skip = parent->skips[i][current_skip] + node_skips;
    }
    else
    {
      node |= 1 << child_mask;
      sub_tree_alloc_children(*parent, i + 1, parent->skips[i][current_skip] + node_skips);
      sub_tree_increase_skips(*parent, i, current_skip);
      current_skip = parent->skips[i][current_skip] + node_skips;
    }
  }

  auto &node = parent->nodes[4][current_skip];
  auto child_mask = morton::morton_get_child_mask(lod, child->morton_min);
  int node_skips = sub_tree_count_skips(node, child_mask);
  int current_skip_and_node = current_skip + node_skips;
  if (node & (1 << child_mask))
  {
    auto *sub_tree = tree_cache.get(parent->sub_trees[current_skip_and_node]);
    insert_tree_in_tree(tree_cache, sub_tree->id, child->id);
  }
  else
  {
    node |= 1 << child_mask;
    if (parent->magnitude - 1 == child->magnitude)
    {
      parent->sub_trees.emplace(parent->sub_trees.begin() + current_skip_and_node, child_id);
      sub_tree_increase_skips(*parent, 4, current_skip);
    }
    else
    {
      auto &sub_tree = tree_cache_add_tree(tree_cache, parent);
      parent = tree_cache.get(parent_id);
      child = tree_cache.get(child_id);
      parent->sub_trees.emplace(parent->sub_trees.begin() + current_skip_and_node, sub_tree.id);
      sub_tree_increase_skips(*parent, 4, current_skip);
      tree_initialize_sub(*parent, tree_cache, child->morton_min, sub_tree);
      insert_tree_in_tree(tree_cache, sub_tree.id, child->id);
    }
  }
}

static tree_id_t reparent_tree(tree_cache_t &tree_cache, tree_id_t tree_id, const morton::morton192_t &possible_min, const morton::morton192_t &possible_max)
{
  tree_t *tree = tree_cache.get(tree_id);
  tree_t &new_parent = tree_cache_add_tree(tree_cache, tree);
  tree_initialize_new_parent(*tree, possible_min, possible_max, new_parent);
  assert(new_parent.magnitude != tree->magnitude);

  insert_tree_in_tree(tree_cache, new_parent.id, tree->id);
  return new_parent.id;
}


tree_id_t tree_add_points(const tree_global_state_t &state, tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const storage_header_t &header)
{
  tree_id_t ret = tree_id;
  auto *tree = tree_cache.get(tree_id);
  //assert(validate_points_offset(header));
  if (header.morton_min < tree->morton_min || tree->morton_max < header.morton_max )
  {
    ret = reparent_tree(tree_cache, tree_id, header.morton_min, header.morton_max);
    tree = tree_cache.get(ret);
  }

  points_collection_t points_data;
  points_data_initialize(points_data, header);
  auto min = tree->morton_min;
  sub_tree_insert_points(state, tree_cache, cache, tree->id, min, 0, 0, std::move(points_data));
  return ret;
}
}
} // namespace points
