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
#include "tree_lod_generator.hpp"

#include <fmt/printf.h>

namespace points
{
namespace converter
{

struct children_subset_t
{
  std::vector<points_subset_t> data;
  std::vector<int> data_skips;
  std::vector<int> skips;
};

static input_data_id_t get_next_input_id(tree_cache_t &tree_cache)
{
  input_data_id_t ret;
  static_assert(sizeof(ret) == sizeof(tree_cache.current_lod_node_id), "input_data_id_t is incompatible with tree_cache_t::current_lod_node_id");
  memcpy(&ret, &tree_cache.current_lod_node_id, sizeof(ret));
  tree_cache.current_lod_node_id++;
  return ret;
}

std::pair<int,int> find_missing_lod(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t tree_id,  const morton::morton192_t &min, const morton::morton192_t &max, const morton::morton192_t &parent_min, const morton::morton192_t &parent_max, int current_level, int skip, children_subset_t &to_lod)
{
  auto tree = tree_cache.get(tree_id);
  assert(skip < int(tree->nodes[current_level].size()));
  auto &node = tree->nodes[current_level][skip];
  assert(node || (tree->data[current_level][skip].point_count > 0 && tree->data[current_level][skip].point_count < uint64_t(-1)));
  if (!node)
  {
    const auto &data = tree->data[current_level][skip];
    assert(data.data.size());
    to_lod.data.insert(to_lod.data.end(), data.data.cbegin(), data.data.cend());
    int to_ret = data.data.size();
    to_lod.data_skips.push_back(to_ret);
    to_lod.skips.push_back(1);
    return std::make_pair(1, to_ret);
  }
  auto &node_data = tree->data[current_level][skip].data;
  assert(node_data.size() == 0);
  int skip_index = int(to_lod.skips.size());
  node_data.emplace_back(get_next_input_id(tree_cache), offset_t(0), point_count_t(0));
  to_lod.data.emplace_back(node_data.back());
  to_lod.data_skips.emplace_back();
  to_lod.skips.emplace_back(0);
  int lod = morton::morton_tree_level_to_lod(tree->magnitude, current_level);
  int child_count = 0;
  auto ret_pair = std::make_pair(1,1);
  int sub_skip_parent = tree->skips[current_level][skip];
  for (int i = 0; i < 8; i++)
  {
    const bool has_this_child = node & (1 << i);
    if (has_this_child)
    {
      child_count++;
      morton::morton192_t child_min = parent_min;
      morton::morton_set_child_mask(lod, i, child_min);
      if (max < child_min)
        break;
      morton::morton192_t child_max = parent_max;
      morton::morton_set_child_mask(lod, i, child_max);
      if (child_max < min)
        continue;
      int sub_skip = sub_skip_parent + child_count - 1;
      std::pair<int,int> adjust;
      if (current_level == 4)
      {
        assert(sub_skip < int(tree->sub_trees.size()));
        tree_t *sub_tree = tree_cache.get(tree->sub_trees[sub_skip]);
        adjust = find_missing_lod(tree_cache, cache, sub_tree->id, min, max, child_min, child_max, 0, 0, to_lod);
      }
      else
      {
        adjust = find_missing_lod(tree_cache, cache, tree_id, min, max, child_min, child_max, current_level + 1, sub_skip, to_lod);
      }
      ret_pair.first += adjust.first;
      ret_pair.second += adjust.second;
      to_lod.skips[skip_index] += adjust.first;
      to_lod.data_skips[skip_index] += adjust.second;
    }
  }
  return ret_pair;
}

void tree_get_work_items(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const morton::morton192_t &min, const morton::morton192_t &max)
{
  auto tree = tree_cache.get(tree_id);
  children_subset_t to_lod;
  find_missing_lod(tree_cache, cache, tree_id, min, max, tree->morton_min, tree->morton_max, 0,0, to_lod);
  fmt::print("{}\n", to_lod.data.size());
}

}
}
