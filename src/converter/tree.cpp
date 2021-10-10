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
#include "tree_p.h"
#include <assert.h>
#include <points/converter/default_attribute_names.h>
#include "point_buffer_splitter_p.h"

namespace points
{
namespace converter
{

static int get_skip_index(uint8_t child_mask, int child_index)
{
  assert(child_index < 8);
  int skip = 0;
  uint8_t mask = 1;
  for (int i = 0; i < child_index; i++, mask <<= 1)
  {
    if (child_mask & mask)
      skip++;
  }
  return skip;
}

static void insert_tree_in_tree(tree_t &parent, tree_t &&child)
{
  assert(memcmp(morton::morton_and(morton::morton_negate(morton::morton_xor(parent.morton_min, parent.morton_max)), child.morton_min).data, parent.morton_min.data, sizeof(parent.morton_min)) == 0);
  int current_skip = 0;
  for (int i = 0; i < 5; i++)
  {
    uint8_t child_index = morton::morton_get_child_mask(morton::morton_magnitude_to_lod(tree.magnitude) - i, tree.morton_min);
    int skip_index = get_skip_index(new_parent.nodes[i][current_skip], child_index);
    if (new_parent.nodes[i][current_skip] & (1 << child_index))
    {
      int next_skip = new_parent.skips[i][current_skip] + skip_index;
      assert(next_skip >= 0);
      current_skip = next_skip;
    }
    else
    {
      new_parent.nodes[i][current_skip] |= (1 << child_index);
      //new_parent.nodes[i][]
      //new_parent.nodes[i]
    }
  }

}

static void reparent_tree(tree_t &new_parent, tree_t &&tree, const morton::morton64_t &possible_min, const morton::morton64_t &possible_max)
{
  morton::morton64_t new_min = tree.morton_min < possible_min ? tree.morton_min : possible_min;
  morton::morton64_t new_max = tree.morton_max < possible_max ? possible_max : tree.morton_max;

  int lod = morton::morton_lod(new_min, new_max);
  new_parent.magnitude = morton::morton_magnitude_from_lod(lod);
  morton::morton64_t new_tree_mask = morton::morton_mask_create(lod);
  morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  new_parent.morton_min = morton::morton_and(new_tree_mask_inv, new_min);
  new_parent.morton_max = morton::morton_or(new_parent.morton_min, new_tree_mask);

  assert(new_parent.magnitude != tree.magnitude);

  insert_tree_in_tree(new_parent, std::move(tree));
}

void tree_initialize(const tree_global_state_t &global_state, tree_t &tree, points_t &&points)
{
  morton::morton64_t mask = morton::morton_xor(points.header.morton_min, points.header.morton_max);
  int magnitude = morton::morton_magnitude_from_bit_index(morton::morton_msb(mask));
  morton::morton64_t new_tree_mask = morton::morton_mask_create(morton::morton_magnitude_to_lod(magnitude));
  morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(points.header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(points.header.morton_min, new_tree_mask);
  tree.magnitude = uint8_t(magnitude);
  (void) global_state; //should subdivide?
  tree.nodes[0].push_back(0);
  tree.skips[0].push_back(int16_t(0));
  tree.data[0].emplace_back();
  tree.data[0][0].point_count += points.header.point_count;

  points_data_initialize(tree.data[0][0], std::move(points));

  {
    morton::morton64_t node_mask = tree.morton_max;
    morton::morton_add_one(node_mask);
    for (auto &p : tree.data[0][0].data)
    {
      switch (p.header.attributes.attributes[0].format)
      {
      case format_i32:
        verify_points_less_than<int32_t>(global_state, p, 0, int(p.header.point_count), node_mask);
        break;
      default:
        assert(false);
        break;
      }
    }
  }
}

void tree_initialize_sub(const tree_t &parent_tree, const morton::morton64_t &morton, tree_t &sub_tree)
{
  sub_tree.magnitude = parent_tree.magnitude - 1;
  morton::morton64_t new_tree_mask = morton::morton_mask_create(sub_tree.magnitude * 5 + 4);
  morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  sub_tree.morton_min = morton::morton_and(morton, new_tree_mask_inv);
  sub_tree.morton_max = morton::morton_or(morton, new_tree_mask);
  sub_tree.nodes[0].push_back(0);
  sub_tree.skips[0].push_back(int16_t(0));
  sub_tree.data[0].emplace_back();
}

static void sub_tree_alloc_children(tree_t &tree, int level, int skip, int count)
{
  for (int i = 0; i < count; i++)
  {
    tree.nodes[level].emplace(tree.nodes[level].begin() + skip);
    tree.skips[level].emplace(tree.skips[level].begin() + skip);
    tree.data[level].emplace(tree.data[level].begin() + skip);
    if (level == 4)
      tree.sub_trees.emplace(tree.sub_trees.begin() + skip);
  }
}

static void sub_tree_increase_skips(tree_t &tree, int level, int skip, int count)
{
  auto &skips = tree.skips[level];
  auto skips_size = skips.size();
  for (int i = skip + 1; i < int(skips_size); i++)
  {
    skips[i] += int16_t(count);
  }
}

//static void sub_tree_insert_points_handle_children(tree_t &tree, int level, int skip, std::vector<points_t> &points)
//{
//
//}

static int sub_tree_count_skips(uint8_t node, int index)
{
  int node_skips = 0;
  for (int i = 1; i < index; i<<=1)
  {
    if (node & uint8_t(i))
      node_skips++;
  }
  return node_skips;
}

static void sub_tree_split_points_to_children(const tree_global_state_t &state, points_data_t &&points, int lod, const morton::morton64_t &node_min, points_data_t (&children)[8])
{
  for (auto &p : points.data)
  {
    assert(p.header.attributes.attributes.size());
    assert(strcmp(p.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ) == 0);
    uint32_t offsets[8];

    point_buffer_get_child_offsets(state, p, lod, node_min, offsets);
    point_buffer_split_buffers_to_children(state, p, offsets, children);
  }
  points = points_data_t();
}

static void sub_tree_insert_points(const tree_global_state_t &state, tree_t &tree, int current_level, int skip, points_data_t &&points)
{
  auto &node = tree.nodes[current_level][skip];
  int lod = morton::morton_tree_level_to_lod(tree.magnitude, current_level);
  auto child_mask = morton::morton_get_child_mask(lod, points.morton_min);
  assert(child_mask < 8);
  if (lod >= points.min_lod)
  {
    if (node & (1 << child_mask))
    {
      int node_skips = sub_tree_count_skips(node, child_mask);
      if (current_level == 4)
      {
        auto &sub_tree = tree.sub_trees[skip + node_skips];
        sub_tree_insert_points(state, sub_tree, 0, 0, std::move(points)); 
      } 
      else
      {
        sub_tree_insert_points(state, tree, current_level + 1, tree.skips[current_level][skip] + node_skips, std::move(points));
      }
      return;
    }
    else if (node)
    {
      int node_skips = sub_tree_count_skips(node, child_mask);

      sub_tree_increase_skips(tree, current_level, skip, 1);
      tree.nodes[current_level][skip] |= 1 << child_mask;
     
      if (current_level == 4)
      {
        tree.sub_trees.emplace(tree.sub_trees.begin() + skip + node_skips);
        auto &sub_tree = tree.sub_trees[skip + node_skips];
        tree_initialize_sub(tree, points.morton_min, sub_tree);
        sub_tree_insert_points(state, sub_tree, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_alloc_children(tree, current_level + 1, tree.skips[current_level][skip] + node_skips, 1);
        sub_tree_insert_points(state, tree, current_level + 1, tree.skips[current_level][skip] + node_skips, std::move(points));
      }
      return;
    }
    else if (tree.data[current_level][skip].point_count + points.point_count <= state.node_limit)
    {
      points_data_add(tree.data[current_level][skip], std::move(points));
      return;
    }
  }
  assert(points.point_count);
  assert(tree.magnitude > 0 || current_level < 4);
  {
    morton::morton64_t node_min = morton::morton_and(morton::morton_negate(morton::morton_mask_create(lod)), points.morton_min);
    points_data_t children_data[8];
    if (!node && tree.data[current_level][skip].point_count)
    {
      sub_tree_split_points_to_children(state, std::move(tree.data[current_level][skip]), lod, node_min, children_data);
      tree.data[current_level][skip].point_count = 0;
    }
    if (points.point_count)
      sub_tree_split_points_to_children(state, std::move(points), lod, node_min, children_data);
   
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

      assert(child_data.min_lod <= lod);

      if (current_level == 4)
      {
        if (node & (1 << i))
        {
          sub_tree_insert_points(state, tree.sub_trees[skip + child_count], 0, 0, std::move(child_data)); 
        }
        else
        {
          sub_tree_increase_skips(tree, current_level, skip, 1);
          tree.nodes[current_level][skip] |= uint8_t(1) << i;
          tree.sub_trees.emplace(tree.sub_trees.begin() + skip + child_count);
          auto &sub_tree = tree.sub_trees[skip + child_count];
          tree_initialize_sub(tree, child_data.morton_min, sub_tree);
          sub_tree_insert_points(state, sub_tree, 0, 0, std::move(child_data));
        }
      }
      else
      {
        if (node & (1 << i))
        {
          sub_tree_insert_points(state, tree, current_level + 1, tree.skips[current_level][skip] + child_count, std::move(child_data));
        }
        else
        {
          sub_tree_increase_skips(tree, current_level, skip, 1);
          tree.nodes[current_level][skip] |= uint8_t(1) << i;
          sub_tree_alloc_children(tree, current_level + 1, tree.skips[current_level][skip] + child_count, 1);
          sub_tree_insert_points(state, tree, current_level + 1, tree.skips[current_level][skip] + child_count, std::move(child_data));
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
  if (scaled_offset % min_offset != 0)
    return false;
  return min_offset <= scaled_offset;
}

static bool validate_points_offset(const points_t &points)
{
  const double (&offsets)[3] = points.header.offset;
  if (offsets[0] == 0.0 && offsets[1] == 0.0 && offsets[2] == 0.0)
    return true;
  uint64_t scaled_offsets[3];
  scaled_offsets[0] = uint64_t(offsets[0] / points.header.scale[0]); 
  scaled_offsets[1] = uint64_t(offsets[1] / points.header.scale[1]); 
  scaled_offsets[2] = uint64_t(offsets[2] / points.header.scale[2]);

  uint64_t min_offset = uint64_t(1) << points.header.lod_span;
  return validate_min_offset(min_offset, scaled_offsets[0])
    && validate_min_offset(min_offset, scaled_offsets[1])
    && validate_min_offset(min_offset, scaled_offsets[2]);
}

void tree_add_points(const tree_global_state_t &state, tree_t &tree, points_t &&points)
{
  assert(validate_points_offset(points));
  if (points.header.morton_min < tree.morton_min || tree.morton_max < points.header.morton_max )
  {
    tree_t old_tree = std::move(tree);
    reparent_tree(tree, std::move(old_tree), points.header.morton_min, points.header.morton_max);
  }

  points_data_t points_data;
  points_data_initialize(points_data, std::move(points));
  sub_tree_insert_points(state, tree, 0, 0, std::move(points_data));
}
}
} // namespace points
