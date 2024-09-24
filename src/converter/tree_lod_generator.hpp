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
#pragma once

#include "threaded_event_loop.hpp"
#include "tree.hpp"
#include "worker.hpp"

#include <ankerl/unordered_dense.h>
#include <deque>

namespace points
{
namespace converter
{

struct lod_child_storage_info_t
{
  attributes_id_t attributes_id;
  std::vector<storage_location_t> locations;
};

struct input_data_id_hash_t
{
  using is_avalanching = void;
  auto operator()(input_data_id_t id) const noexcept -> uint64_t
  {
    uint64_t data;
    static_assert(sizeof(data) == sizeof(id), "size mismatch");
    memcpy(&data, &id, sizeof(data));
    return ankerl::unordered_dense::detail::wyhash::hash(data);
  }
};

using child_storage_map_t = ankerl::unordered_dense::map<input_data_id_t, lod_child_storage_info_t, input_data_id_hash_t>;
struct lod_node_worker_data_t
{
  morton::morton192_t node_min;
  uint16_t id;
  uint16_t lod;
  input_data_id_t storage_name;
  std::vector<points_collection_t> child_data;
  std::vector<tree_id_t> child_trees;
  child_storage_map_t child_storage_info;
  point_count_t generated_point_count;
  morton::morton192_t generated_min;
  morton::morton192_t generated_max;
  attributes_id_t generated_attributes_id;
  std::vector<storage_location_t> generated_locations;
};

struct lod_tree_worker_data_t
{
  tree_id_t tree_id;
  int magnitude;
  std::vector<lod_node_worker_data_t> nodes[5];
};

class tree_lod_generator_t;
class attributes_configs_t;
struct lod_worker_batch_t;
class lod_worker_t : public worker_t
{
public:
  lod_worker_t(tree_lod_generator_t &lod_generator, lod_worker_batch_t &batch, storage_handler_t &cache, attributes_configs_t &attributes_configs, lod_node_worker_data_t &data, const std::vector<float> &random_offsets);
  void work() override;
  void after_work(completion_t completion) override;

private:
  tree_lod_generator_t &lod_generator;
  lod_worker_batch_t &batch;
  storage_handler_t &cache;
  attributes_configs_t &attributes_configs;
  lod_node_worker_data_t &data;
  const std::vector<float> &random_offsets;
};

struct lod_worker_batch_t
{
  std::vector<lod_tree_worker_data_t> worker_data;
  std::vector<lod_worker_t> lod_workers;
  std::atomic_int completed = 0;
  int batch_size = 0;
  int level = 5;
  bool new_batch = true;
};

class tree_lod_generator_t
{
public:
  tree_lod_generator_t(threaded_event_loop_t &loop, tree_registry_t &tree_cache, storage_handler_t &file_cache, attributes_configs_t &attributes_configs, event_pipe_t<void> &lod_done);
  void generate_lods(tree_id_t &tree_id, const morton::morton192_t &max);

  void iterate_workers();

  void add_worker_done(lod_worker_batch_t &batch)
  {
    if (++batch.completed == batch.batch_size)
    {
      _iterate_workers.post_event();
    }
  }

private:
  threaded_event_loop_t &_loop;
  tree_registry_t &_tree_cache;
  storage_handler_t &_file_cache;
  attributes_configs_t &_attributes_configs;
  event_pipe_t<void> &_lod_done;
  event_pipe_t<void> _iterate_workers;

  std::vector<float> _random_offsets;

  std::deque<std::unique_ptr<lod_worker_batch_t>> _lod_batches;
};

} // namespace converter
} // namespace points
