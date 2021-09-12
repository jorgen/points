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

static void reparent_tree(tree_t &new_parent, tree_t &&tree)
{
  assert(new_parent.level != tree.level);
  if (morton::morton_null(tree.morton_max))
    return;
  //assert that child is indeed a child of parent
  assert(memcmp(morton::morton_and(morton::morton_negate(morton::morton_xor(new_parent.morton_min, new_parent.morton_max)), tree.morton_min).data, new_parent.morton_min.data, sizeof(new_parent.morton_min)) == 0);


  int current_skip = 0;
  for (int i = 0; i < 5; i++)
  {
    uint8_t child_index = morton::morton_get_child_mask(morton::morton_tree_lod_top_lod(tree.level) - i, tree.morton_min);
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

void tree_initialize(const tree_global_state_t &global_state, tree_t &tree, points_t &&points)
{
  morton::morton64_t mask = morton::morton_xor(points.header.morton_min, points.header.morton_max);
  int new_tree_lod = morton::morton_tree_lod(morton::morton_msb(mask));
  morton::morton64_t new_tree_mask = morton::morton_mask_create(new_tree_lod * 5 + 4);
  morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(points.header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(points.header.morton_min, new_tree_mask);
  tree.level = uint8_t(new_tree_lod);
  (void) global_state; //should subdivide?
  tree.nodes[0].push_back(0);
  tree.skips[0].push_back(int16_t(0));
  tree.data[0].emplace_back();
  tree.data[0][0].point_count += points.header.point_count;

  points_data_initialize(tree.data[0][0], std::move(points));
}
        
void tree_initialize_sub(const tree_t &parent_tree, const morton::morton64_t &morton, tree_t &sub_tree)
{
  sub_tree.level = parent_tree.level - 1;
  morton::morton64_t new_tree_mask = morton::morton_mask_create(sub_tree.level * 5 + 4);
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

static void sub_tree_split_points_to_children(const tree_global_state_t &state, points_data_t &&points, int level, points_data_t (&children)[8])
{
  for (auto &p : points.data)
  {
    assert(p.header.attributes.attributes.size());
    assert(strcmp(p.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ) == 0);
    uint32_t offsets[8];
    point_buffer_get_child_offsets(state, p, level, offsets);
    point_buffer_split_buffers_to_children(state, p, offsets, children);
  }
  points = points_data_t();
}

static void sub_tree_insert_points(const tree_global_state_t &state, tree_t &tree, int level, int skip, points_data_t &&points)
{
  auto &node = tree.nodes[level][skip];
  int lod = morton::morton_tree_lod_to_lod(tree.level, level);
  auto child_mask = morton::morton_get_child_mask(lod, points.morton_min);
  assert(child_mask < 8);
  if (lod >= points.lod_span)
  {
    if (node & (1 << child_mask))
    {
      int node_skips = sub_tree_count_skips(node, child_mask);
      if (level == 4)
      {
        sub_tree_insert_points(state, tree.sub_trees[skip + node_skips], 0, 0, std::move(points)); 
      } 
      else
      {
        sub_tree_insert_points(state, tree, level + 1, tree.skips[level][skip] + node_skips, std::move(points));
      }
      return;
    }
    else if (node)
    {
      int node_skips = sub_tree_count_skips(node, child_mask);

      sub_tree_increase_skips(tree, level, skip, 1);
      tree.nodes[level][skip] |= 1 << child_mask;
     
      if (level == 4)
      {
        tree.sub_trees.emplace(tree.sub_trees.begin() + skip + node_skips);
        auto &sub_tree = tree.sub_trees[skip + node_skips];
        tree_initialize_sub(tree, points.morton_min, sub_tree);
        sub_tree_insert_points(state, sub_tree, 0, 0, std::move(points));
      }
      else
      {
        sub_tree_alloc_children(tree, level + 1, tree.skips[level][skip] + node_skips, 1);
        sub_tree_insert_points(state, tree, level + 1, tree.skips[level][skip] + node_skips, std::move(points));
      }
      return;
    }
    else if (tree.data[level][skip].point_count + points.point_count < state.node_limit)
    {
      points_data_add(tree.data[level][skip], std::move(points));
      return;
    }
//    else if (lod > points.lod_span)
//    {
//      int node_skips = sub_tree_count_skips(node, child_mask);
//      sub_tree_increase_skips(tree, level, skip, 1);
//      tree.nodes[level][skip] |= uint8_t(1) << child_mask;
//      auto data = std::move(tree.data[level][skip]);
//      points_data_add(data, std::move(points));
//      if (level == 4)
//      {
//        tree.sub_trees.emplace(tree.sub_trees.begin() + skip + node_skips);
//        auto &sub_tree = tree.sub_trees[skip + node_skips];
//        tree_initialize_sub(tree, points.morton_min, sub_tree);
//        sub_tree_insert_points(state, sub_tree, 0, 0, std::move(data));
//      }
//      else
//      {
//        sub_tree_alloc_children(tree, level + 1, tree.skips[level][skip] + node_skips, 1);
//        sub_tree_insert_points(state, tree, level + 1, tree.skips[level][skip] + node_skips, std::move(data));
//      }
//      return;
//    }
  }

  {
    points_data_t children_data[8];
    if (points.point_count)
      sub_tree_split_points_to_children(state, std::move(points), lod, children_data);
    if (!node && tree.data[level][skip].point_count)
      sub_tree_split_points_to_children(state, std::move(tree.data[level][skip]), lod, children_data);
   
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

      if (level == 4)
      {
        if (node & (1 << i))
        {
          sub_tree_insert_points(state, tree.sub_trees[skip + child_count], 0, 0, std::move(child_data)); 
        }
        else
        {
          sub_tree_increase_skips(tree, level, skip, 1);
          tree.nodes[level][skip] |= uint8_t(1) << i;
          tree.sub_trees.emplace(tree.sub_trees.begin() + skip + child_count);
          auto &sub_tree = tree.sub_trees[skip + child_count];
          tree_initialize_sub(tree, points.morton_min, sub_tree);
          sub_tree_insert_points(state, sub_tree, 0, 0, std::move(child_data));
        }
      }
      else
      {
        if (node & (1 << i))
        {
          sub_tree_insert_points(state, tree, level + 1, tree.skips[level][skip] + child_count, std::move(child_data));
        }
        else
        {
          sub_tree_increase_skips(tree, level, skip, 1);
          tree.nodes[level][skip] |= uint8_t(1) << i;
          sub_tree_alloc_children(tree, level + 1, tree.skips[level][skip] + child_count, 1);
          sub_tree_insert_points(state, tree, level + 1, tree.skips[level][skip] + child_count, std::move(child_data));
        }
      }
      child_count++;
    }
  }
}

void tree_add_points(const tree_global_state_t &state, tree_t &tree, points_t &&points)
{
  morton::morton64_t points_to_insert_mask = morton::morton_xor(points.header.morton_min, points.header.morton_max);
  int points_to_insert_tree_lod = morton::morton_tree_lod(morton::morton_msb(points_to_insert_mask));
  if (tree.level < uint8_t(points_to_insert_tree_lod))
  {
    tree_t old_tree = std::move(tree);
//    morton::morton64_t new_tree_mask = morton::morton_mask_create(points_to_insert_tree_lod * 5 + 4);
//    morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
//    tree.morton_min = morton::morton_and(points.header.morton_min, new_tree_mask_inv);
//    tree.morton_max = morton::morton_or(points.header.morton_min, new_tree_mask);
//    tree.level = uint8_t(new_tree_lod);
    reparent_tree(tree, std::move(old_tree));
  }

  points_data_t points_data;
  points_data_initialize(points_data, std::move(points));
  sub_tree_insert_points(state, tree, 0, 0, std::move(points_data));
}
}
} // namespace points
