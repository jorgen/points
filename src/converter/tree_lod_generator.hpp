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

#include "tree.hpp"
#include "worker.hpp"
#include "threaded_event_loop.hpp"

#include <deque>

namespace points
{
namespace converter
{

struct lod_node_worker_data_t
{
  uint16_t id;
  uint16_t lod;
  input_data_id_t storage_name;
  std::vector<points_subset_t> child_data;
  point_count_t generated_point_count;
};

struct lod_tree_worker_data_t
{
  tree_id_t tree_id;
  int magnitude;
  std::vector<lod_node_worker_data_t> nodes[5];
};

class tree_lod_generator_t;
class attributes_configs_t;

class lod_worker_t : public worker_t
{
public:
  lod_worker_t(tree_lod_generator_t &lod_generator, cache_file_handler_t &cache, attributes_configs_t &attributes_configs, lod_node_worker_data_t &data, int &inc_on_completed);
  void work() override;
  void after_work(completion_t completion) override;
private:
  tree_lod_generator_t &lod_generator;
  cache_file_handler_t &cache;
  attributes_configs_t &attributes_configs;
  lod_node_worker_data_t &data;
  int &inc_on_completed;
};

struct lod_tree_worker_batch
{

};

struct lod_worker_batch_t
{
  std::vector<lod_tree_worker_data_t> worker_data;
  std::vector<lod_worker_t> lod_workers;
  int completed = 0;
  int level = 5;
  bool new_batch = true;
};

class tree_lod_generator_t
{
public:
  tree_lod_generator_t(threaded_event_loop_t &loop, const tree_global_state_t &tree_global_state, tree_cache_t &tree_cache, cache_file_handler_t &file_cache, attributes_configs_t &attributes_configs);
  void generate_lods(tree_id_t &tree_id, const morton::morton192_t &max);

  void iterate_workers();

  const tree_global_state_t &global_state() const { return _tree_global_state; }
private:
  threaded_event_loop_t &_loop;
  const tree_global_state_t &_tree_global_state;
  tree_cache_t &_tree_cache;
  cache_file_handler_t &_file_cache;
  attributes_configs_t &_attributes_configs;

  std::deque<std::unique_ptr<lod_worker_batch_t>> _lod_batches;

  morton::morton192_t _generated_until;
};

}
}
