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

void tree_initialize(tree_t &tree, int node_limit, points_t &&points)
{
  morton::morton64_t mask = morton::morton_xor(points.header.morton_min, points.header.morton_max);
  int new_tree_lod = morton::morton_tree_lod(morton::morton_msb(mask));
  morton::morton64_t new_tree_mask = morton::morton_mask_create(new_tree_lod * 5 + 4);
  morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
  tree.morton_min = morton::morton_and(points.header.morton_min, new_tree_mask_inv);
  tree.morton_max = morton::morton_or(points.header.morton_min, new_tree_mask);
  tree.node_limit = node_limit;
  tree.level = uint8_t(new_tree_lod);
  tree.nodes[0].push_back(0);
  tree.skips[0].push_back(int16_t(-1));
  tree.data[0].emplace_back();
  tree.data[0][0].point_count += points.header.point_count;
  tree.data[0][0].data.emplace_back(std::move(points));
}

//static void sub_tree_divide_points(int level, points_t &&points, std::vector<points_t> &node_data, std::vector<points_t> (&child_data)[8])
//{
//
//}
//


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
    skips[i] += count;
  }
}

//static void sub_tree_insert_points_handle_children(tree_t &tree, int level, int skip, std::vector<points_t> &points)
//{
//
//}

static int sub_tree_count_skips(uint8_t node, int index)
{
  int node_skips = 0;
  for (int i = 0; i < node; i++)
  {
    if (node & (1 << i))
      node_skips++;
  }
  return 0;
}

static void sub_tree_insert_points(tree_t &tree, int level, int skip, points_t &&points)
{
  int node = tree.nodes[level][skip];
  (void)node;
  int lod = morton::morton_tree_lod_to_lod(tree.level, level);
  auto child_mask = morton::morton_get_child_mask(lod, points.header.morton_max);
  assert(child_mask < 8);
  if (lod >= points.header.lod_span)
  {
    if (node & (1 << child_mask))
    {
      int node_skips = sub_tree_count_skips(node, child_mask);
      sub_tree_insert_points(tree, level + 1, tree.skips[level][skip] + node_skips, std::move(points));
      return;
    }
    else if (node)
    {
      int node_skips = sub_tree_count_skips(node, child_mask);
      sub_tree_alloc_children(tree, level + 1, tree.skips[level][skip] + node_skips, 1);
      sub_tree_increase_skips(tree, level, skip, 1);
      sub_tree_insert_points(tree, level + 1, tree.skips[level][skip] + node_skips, std::move(points));
      return;
    }
    else if (tree.data[level][skip].point_count + points.header.point_count < tree.node_limit)
    {
      tree.data[level][skip].point_count += points.header.point_count;
      tree.data[level][skip].data.emplace_back(std::move(points));
      return;
    }
  }

  {
    std::vector<points_t> children_data[8];
    int children_count = 0;
    //int child_skip = tree.skips[level][skip];
    sub_tree_alloc_children(tree, level + 1, tree.skips[level][skip], children_count);
    sub_tree_increase_skips(tree, level, skip, children_count);
    for (int i= 0; i < 8; i++)
    {
      auto &child_data = children_data[i];
      if (child_data.empty())
        continue;
      tree.nodes[level][skip] |= uint8_t(1) << i;
      for (auto &child_points : child_data)
      {
        sub_tree_insert_points(tree, level + 1, tree.skips[level][skip] + i, std::move(child_points));
      }
    }
  }
}

void tree_add_points(tree_t &tree, points_t &&points)
{
  morton::morton64_t points_to_insert_mask = morton::morton_xor(tree.morton_min, points.header.morton_min);
  int new_tree_lod = morton::morton_tree_lod(morton::morton_msb(points_to_insert_mask));
  if (tree.level < uint8_t(new_tree_lod))
  {
    tree_t old_tree = std::move(tree);
    morton::morton64_t new_tree_mask = morton::morton_mask_create(new_tree_lod * 5 + 4);
    morton::morton64_t new_tree_mask_inv = morton::morton_negate(new_tree_mask);
    tree.morton_min = morton::morton_and(points.header.morton_min, new_tree_mask_inv);
    tree.morton_max = morton::morton_or(points.header.morton_min, new_tree_mask);
    tree.level = uint8_t(new_tree_lod);
    reparent_tree(tree, std::move(old_tree));
  }

  sub_tree_insert_points(tree, 0, 0, std::move(points));
}
}
} // namespace points
