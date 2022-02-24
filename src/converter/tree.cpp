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
#include "data_provider.hpp"
namespace points
{
namespace converter
{

void tree_initialize(const tree_global_state_t &global_state, cache_file_handler_t &cache, tree_t &tree, const internal_header_t &header)
{
  morton::morton192_t mask = morton::morton_xor(header.morton_min, header.morton_max);
  int magnitude = morton::morton_magnitude_from_bit_index(morton::morton_msb(mask));
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(morton::morton_magnitude_to_lod(magnitude));
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(header.morton_min, new_tree_mask);
  tree.magnitude = uint8_t(magnitude);
  (void) global_state; //should subdivide?
  tree.nodes[0].push_back(0);
  tree.skips[0].push_back(int16_t(0));
  tree.data[0].emplace_back();
  tree.data[0][0].point_count += header.point_count;

  points_data_initialize(tree.data[0][0], header);

  {
    morton::morton192_t node_mask = tree.morton_max;
    morton::morton_add_one(node_mask);
    for (auto &p : tree.data[0][0].data)
    {
      read_points_t read_points(cache, p.input_id);
      switch (read_points.header.point_format)
      {
      case format_m32:
        verify_points_less_than<morton::morton32_t::component_type, morton::morton32_t::component_count::value>(global_state, read_points, 0, int(read_points.header.point_count), node_mask);
        break;
      case format_m64:
        verify_points_less_than<morton::morton64_t::component_type, morton::morton64_t::component_count::value>(global_state, read_points, 0, int(read_points.header.point_count), node_mask);
        break;
      case format_m128:
        verify_points_less_than<morton::morton128_t::component_type, morton::morton128_t::component_count::value>(global_state, read_points, 0, int(read_points.header.point_count), node_mask);
        break;
      case format_m192:
        verify_points_less_than<morton::morton192_t::component_type, morton::morton192_t::component_count::value>(global_state, read_points, 0, int(read_points.header.point_count), node_mask);
        break;
      default:
        assert(false);
        break;
      }
    }
  }
}

void tree_initialize_sub(const tree_t &parent_tree, const morton::morton192_t &morton, tree_t &sub_tree)
{
  sub_tree.magnitude = parent_tree.magnitude - 1;
  morton::morton192_t new_tree_mask = morton::morton_mask_create<uint64_t, 3>(sub_tree.magnitude * 5 + 4);
  morton::morton192_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  sub_tree.morton_min = morton::morton_and(morton, new_tree_mask_inv);
  sub_tree.morton_max = morton::morton_or(morton, new_tree_mask);
  sub_tree.nodes[0].push_back(0);
  sub_tree.skips[0].push_back(int16_t(0));
  sub_tree.data[0].emplace_back();
}

void tree_initialize_new_parent(const tree_t &some_child, const morton::morton192_t possible_min, const morton::morton192_t possible_max, tree_t &new_parent)
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
  new_parent.skips[0].push_back(int16_t(0));
  new_parent.data[0].emplace_back();
}


static void sub_tree_alloc_children(tree_t &tree, int level, int skip)
{
  tree.nodes[level].emplace(tree.nodes[level].begin() + skip);
  uint16_t old_skip = size_t(skip) >= tree.skips[level].size() ? 0 : tree.skips[level][skip];
  tree.skips[level].emplace(tree.skips[level].begin() + skip, old_skip);
  tree.data[level].emplace(tree.data[level].begin() + skip);
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

static void sub_tree_split_points_to_children(const tree_global_state_t &state, cache_file_handler_t &cache, points_collection_t &&points, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  for (auto &p : points.data)
  {
    read_points_t p_read(cache, p.input_id);
    assert(p_read.data.size);

    point_buffer_subdivide(state, p_read, p, lod, node_min, children);
  }
  points = points_collection_t();
}

static void sub_tree_insert_points(const tree_global_state_t &state, cache_file_handler_t &cache, tree_t &tree, const morton::morton192_t &min, int current_level, int skip, points_collection_t &&points)
{
  assert(current_level != 0 || tree.morton_min == min);
  auto &node = tree.nodes[current_level][skip];
  int lod = morton::morton_tree_level_to_lod(tree.magnitude, current_level);
  auto child_mask = morton::morton_get_child_mask(lod, points.min);
  assert(child_mask < 8);
  morton::morton192_t max = morton::morton_or(min, morton::morton_mask_create<uint64_t, 3>(lod));
  assert(!(points.min < min));
  assert(!(max < points.max));
  if (lod > points.min_lod)
  {
    morton::morton192_t new_min = min;
    morton::morton_set_child_mask(lod, child_mask, new_min);
    if (node & (1 << child_mask))
    {
      int node_skips = sub_tree_count_skips(node, child_mask);
      if (current_level == 4)
      {
        int sub_tree_skip = tree.skips[4][skip] + node_skips;
        auto &sub_tree = tree.sub_trees[sub_tree_skip];
        sub_tree_insert_points(state, cache, sub_tree, new_min, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_insert_points(state, cache, tree, new_min, current_level + 1, tree.skips[current_level][skip] + node_skips, std::move(points));
      }
      return;
    }
    else if (node)
    {
      int node_skips = sub_tree_count_skips(node, child_mask);

      node |= 1 << child_mask;

      if (current_level == 4)
      {
        int sub_tree_skip = tree.skips[4][skip] + node_skips;
        tree.sub_trees.emplace(tree.sub_trees.begin() + sub_tree_skip);
        sub_tree_increase_skips(tree, current_level, skip);
        auto &sub_tree = tree.sub_trees[sub_tree_skip];
        tree_initialize_sub(tree, points.min, sub_tree);
        sub_tree_insert_points(state, cache, sub_tree, new_min, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_alloc_children(tree, current_level + 1, tree.skips[current_level][skip] + node_skips);
        sub_tree_increase_skips(tree, current_level, skip);
        sub_tree_insert_points(state, cache, tree, new_min, current_level + 1, tree.skips[current_level][skip] + node_skips, std::move(points));
      }
      return;
    }
  }

  if (node == 0 && tree.data[current_level][skip].point_count + points.point_count <= state.node_limit)
  {
    assert(!(points.min < min));
    points_data_add(tree.data[current_level][skip], std::move(points));
    return;
  }
  assert(points.point_count);
  assert(tree.magnitude > 0 || current_level < 4);
  {
    morton::morton192_t node_min = morton::morton_and(morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(lod)), points.min);
    assert(node_min == min);
    points_collection_t children_data[8];
    if (!node && tree.data[current_level][skip].point_count)
    {
      sub_tree_split_points_to_children(state, cache, std::move(tree.data[current_level][skip]), lod, node_min, children_data);
      tree.data[current_level][skip].point_count = 0;
    }
    if (points.point_count)
      sub_tree_split_points_to_children(state, cache, std::move(points), lod, node_min, children_data);

    int child_count = 0;
    for (int i= 0; i < 8; i++)
    {
      auto &child_data = children_data[i];
      if (child_data.data.empty())
      {
        if (node & (1 << i))
          child_count++;
        continue;
      }
      morton::morton192_t new_min = min;
      morton::morton_set_child_mask(lod, uint8_t(i), new_min);
      assert(child_data.min_lod <= lod);

      if (current_level == 4)
      {
        int sub_tree_skip = tree.skips[4][skip] + child_count;
        if ((node & (1 << i)) == 0)
        {
          tree.sub_trees.emplace(tree.sub_trees.begin() + sub_tree_skip);
          sub_tree_increase_skips(tree, current_level, skip);
          tree.nodes[current_level][skip] |= uint8_t(1) << i;
          tree_initialize_sub(tree, child_data.min, tree.sub_trees[sub_tree_skip]);
        }
        sub_tree_insert_points(state, cache, tree.sub_trees[sub_tree_skip], new_min, 0, 0, std::move(child_data));
      }
      else
      {
        if (node & (1 << i))
        {
          sub_tree_insert_points(state, cache, tree, new_min, current_level + 1, tree.skips[current_level][skip] + child_count, std::move(child_data));
        }
        else
        {
          tree.nodes[current_level][skip] |= uint8_t(1) << i;
          sub_tree_alloc_children(tree, current_level + 1, tree.skips[current_level][skip] + child_count);
          sub_tree_increase_skips(tree, current_level, skip);
          sub_tree_insert_points(state, cache, tree, new_min, current_level + 1, tree.skips[current_level][skip] + child_count, std::move(child_data));
        }
      }
      child_count++;
    }
  }
}


static bool validate_min_offset(uint64_t min_offset, uint64_t scaled_offset)
{
  if (scaled_offset == 0)
    return true;
//  if (scaled_offset % min_offset != 0)
//    return false;
  return min_offset <= scaled_offset;
}

static bool validate_points_offset(const internal_header_t &header)
{
  const double (&offsets)[3] = header.offset;
  if (offsets[0] == 0.0 && offsets[1] == 0.0 && offsets[2] == 0.0)
    return true;
  uint64_t scaled_offsets[3];
  scaled_offsets[0] = uint64_t(offsets[0] / header.scale[0]);
  scaled_offsets[1] = uint64_t(offsets[1] / header.scale[1]);
  scaled_offsets[2] = uint64_t(offsets[2] / header.scale[2]);

  uint64_t min_offset = uint64_t(1) << header.lod_span;
  return validate_min_offset(min_offset, scaled_offsets[0])
    && validate_min_offset(min_offset, scaled_offsets[1])
    && validate_min_offset(min_offset, scaled_offsets[2]);
}

static void insert_tree_in_tree(tree_t &parent, tree_t &&child)
{
  assert(memcmp(morton::morton_and(morton::morton_negate(morton::morton_xor(parent.morton_min, parent.morton_max)), child.morton_min).data, parent.morton_min.data, sizeof(parent.morton_min)) == 0);
  int current_skip = 0;
  int lod = morton::morton_magnitude_to_lod(parent.magnitude);
  for (int i = 0; i < 4; i++, lod--)
  {
    auto &node = parent.nodes[i][current_skip];
    auto child_mask = morton::morton_get_child_mask(lod, child.morton_min);
    int node_skips = sub_tree_count_skips(node, child_mask);
    if (node & (1 << child_mask))
    {
      current_skip = parent.skips[i][current_skip] + node_skips;
    }
    else
    {
      node |= 1 << child_mask;
      sub_tree_alloc_children(parent, i + 1, parent.skips[i][current_skip] + node_skips);
      sub_tree_increase_skips(parent, i, current_skip);
      current_skip = parent.skips[i][current_skip] + node_skips;
    }
  }

  auto &node = parent.nodes[4][current_skip];
  auto child_mask = morton::morton_get_child_mask(lod, child.morton_min);
  int node_skips = sub_tree_count_skips(node, child_mask);
  int current_skip_and_node = current_skip + node_skips;
  if (node & (1 << child_mask))
  {
    auto &sub_tree = parent.sub_trees[current_skip_and_node];
    insert_tree_in_tree(sub_tree, std::move(child));
  }
  else
  {
    node |= 1 << child_mask;
    parent.sub_trees.emplace(parent.sub_trees.begin() + current_skip_and_node);
    sub_tree_increase_skips(parent, 4, current_skip);
    auto &sub_tree = parent.sub_trees[current_skip_and_node];
    if (parent.magnitude - 1 == child.magnitude)
    {
      sub_tree = std::move(child);
    }
    else
    {
      tree_initialize_sub(parent, child.morton_min, sub_tree);
      insert_tree_in_tree(sub_tree, std::move(child));
    }
  }
}

static void reparent_tree(tree_t &new_parent, tree_t &&tree, const morton::morton192_t &possible_min, const morton::morton192_t &possible_max)
{
  tree_initialize_new_parent(tree, possible_min, possible_max, new_parent);
  assert(new_parent.magnitude != tree.magnitude);

  insert_tree_in_tree(new_parent, std::move(tree));
}


void tree_add_points(const tree_global_state_t &state, cache_file_handler_t &cache, tree_t &tree, const internal_header_t &header)
{
  validate_points_offset(header);
  if (header.morton_min < tree.morton_min || tree.morton_max < header.morton_max )
  {
    tree_t old_tree = std::move(tree);
    reparent_tree(tree, std::move(old_tree), header.morton_min, header.morton_max);
  }

  points_collection_t points_data;
  points_data_initialize(points_data, header);
  sub_tree_insert_points(state, cache, tree, tree.morton_min, 0, 0, std::move(points_data));
}
}
} // namespace points
