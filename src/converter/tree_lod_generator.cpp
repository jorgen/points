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

#include "worker.hpp"
#include "cache_file_handler.hpp"

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
  std::vector<int> lods;
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
  int lod = morton::morton_tree_level_to_lod(tree->magnitude, current_level);
  if (!node)
  {
    const auto &data = tree->data[current_level][skip];
    assert(data.data.size());
    to_lod.data.insert(to_lod.data.end(), data.data.cbegin(), data.data.cend());
    int to_ret = data.data.size();
    to_lod.data_skips.push_back(to_ret);
    to_lod.skips.push_back(1);
    to_lod.lods.push_back(lod);
    return std::make_pair(1, to_ret);
  }

  int skip_index = 0;
  auto ret_pair = std::make_pair(0,0);
  if (min <= parent_min && parent_max <= max)
  {
    auto &node_data = tree->data[current_level][skip];
    assert(node_data.data.size() <= 1);
    if (node_data.data.size() == 1)
    {
      assert(node_data.data.back().offset.data == (~uint64_t(0)));
      to_lod.data.emplace_back(node_data.data.back());
      to_lod.data_skips.emplace_back(1);
      to_lod.skips.emplace_back(1);
      to_lod.lods.push_back(lod);
      return std::make_pair(1,1);
    }
    skip_index = int(to_lod.skips.size());
    node_data.data.emplace_back(get_next_input_id(tree_cache), offset_t(~uint64_t(0)), point_count_t(0));
    node_data.min = parent_min;
    node_data.max = parent_max;
    to_lod.data.emplace_back(node_data.data.back());
    to_lod.data_skips.emplace_back(1);
    to_lod.skips.emplace_back(1);
    to_lod.lods.push_back(lod);
    ret_pair = std::make_pair(1,1);
  }
  int child_count = 0;
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
      if (ret_pair.first > 0)
      {
        to_lod.skips[skip_index] += adjust.first;
        to_lod.data_skips[skip_index] += adjust.second;
      }
    }
  }
  return ret_pair;
}

static void tree_get_work_items(tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const morton::morton192_t &min, const morton::morton192_t &max, std::vector<lod_worker_data_t> &lod_data)
{
  auto tree = tree_cache.get(tree_id);
  children_subset_t to_lod;
  find_missing_lod(tree_cache, cache, tree_id, min, max, tree->morton_min, tree->morton_max, 0,0, to_lod);

  lod_data.reserve(to_lod.skips.size() / 3);

  std::vector<int> parent_end_stack;
  parent_end_stack.reserve(30);
  std::vector<int> parent_stack;
  parent_stack.reserve(30);
  auto data_start = to_lod.data.begin();
  for (int i = 0; i < int(to_lod.skips.size()); i++)
  {
    int current_skip = int(to_lod.skips[i]);
    bool leaf_node = current_skip == 1;
    int data_skip = leaf_node ? to_lod.data_skips[i] : 1;
    if (parent_stack.size())
    {
      auto &parent = lod_data[parent_stack.back()];
      parent.child_data.insert(parent.child_data.end(), data_start, data_start + data_skip);
    }
    if (!leaf_node)
    {
      parent_stack.emplace_back(int(lod_data.size()));
      parent_end_stack.emplace_back(i + current_skip - 1);
      lod_data.emplace_back(to_lod.lods[i], *data_start);
    }
    while(parent_end_stack.size() && parent_end_stack.back() == i)
    {
      parent_stack.pop_back();
      parent_end_stack.pop_back();
    }
    data_start += data_skip;
  }
  std::sort(lod_data.begin(), lod_data.end(), [](const lod_worker_data_t &a, const lod_worker_data_t &b)
  {
    return a.lod < b.lod;
  });
  fmt::print("{}\n", to_lod.data.size());
}

lod_worker_t::lod_worker_t(tree_lod_generator_t &lod_generator, cache_file_handler_t &cache, lod_worker_data_t &data)
  : lod_generator(lod_generator)
  , cache(cache)
  , data(data)
{
  (void)this->cache;
  (void)this->data;
}


void lod_worker_t::work()
{
  uint64_t total_count = 0;
  for (auto &child : data.child_data)
    total_count += child.count.data;

  for (auto &child : data.child_data)
  {
    read_points_t points(cache, child.input_id);
    double ratio = double(child.count.data) / total_count;
    uint64_t child_count = child.count.data * ratio;
    fmt::print(stderr, "lod {} - child {} - {}\n", data.lod, child.count.data, child_count);
  }
}

void lod_worker_t::after_work(completion_t completion)
{
  lod_generator.iterate_workers();
}

static void iterate_batch(tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, cache_file_handler_t &cache_file, threaded_event_loop_t &loop)
{
  assert(batch.completed < int(batch.worker_data.size()));
  if (batch.current_index == int(batch.worker_data.size()))
    return;
  batch.lod_workers.clear();
  int current_lod = batch.worker_data[batch.current_index].lod;
  int batch_index = batch.current_index;
  while(batch_index < int(batch.worker_data.size())
        && current_lod == batch.worker_data[batch.current_index].lod)
    batch_index++;
  int batch_size = batch_index - batch.current_index;
  batch.lod_workers.reserve(batch_size);
  while(batch.current_index < int(batch.worker_data.size())
        && current_lod == batch.worker_data[batch.current_index].lod)
  {
    batch.lod_workers.emplace_back(lod_generator, cache_file, batch.worker_data[batch.current_index]);
    batch.current_index++;
    batch.lod_workers.back().enqueue(loop);
  }
}

tree_lod_generator_t::tree_lod_generator_t(threaded_event_loop_t &loop, tree_cache_t &tree_cache, cache_file_handler_t &file_cache)
  : _loop(loop)
  , _tree_cache(tree_cache)
  , _file_cache(file_cache)
{
  (void)_loop;
}

void tree_lod_generator_t::generate_lods(tree_id_t &tree_id, const morton::morton192_t &max)
{
  _lod_batches.emplace_back(new lod_worker_batch_t());
  auto &batch = *_lod_batches.back();
  auto &worker_data = batch.worker_data;
  tree_get_work_items(_tree_cache, _file_cache, tree_id, _generated_until, max, worker_data);
  iterate_workers();
}

void tree_lod_generator_t::iterate_workers()
{
  if (_lod_batches.size() && _lod_batches.front()->completed == int(_lod_batches.front()->worker_data.size()))
  {
    _lod_batches.erase(_lod_batches.begin());
  }
  if (_lod_batches.size() && _lod_batches.front()->completed == _lod_batches.front()->current_index)
    iterate_batch(*this, *_lod_batches.front(), _file_cache, _loop);

}

}}//namespace
