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
#include "tree_lod_generator.hpp"

namespace points::converter
{

struct waiting_for_root_t
{
  std::condition_variable *cv;
};

class frustum_tree_walker_t;

class tree_handler_t : public about_to_block_t
{
public:
  tree_handler_t(thread_pool_t &thread_pool, storage_handler_t &file_cache, attributes_configs_t &attributes_configs, event_pipe_t<input_data_id_t> &done_input);
  [[nodiscard]] error_t deserialize_tree_registry(std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t tree_registry_blobs_size);
  void request_root();
  void set_tree_initialization_config(const tree_config_t &config);
  void set_tree_initialization_node_limit(uint32_t limit);
  void about_to_block() override;
  void generate_lod(const morton::morton192_t &max);
  tree_config_t tree_config();
  void request_aabb(std::function<void(double *, double *)> function);
  void request_tree(tree_id_t tree_id);
  bool tree_initialized(tree_id_t tree_id);

private:
  void handle_add_points(storage_header_t &&header, attributes_id_t &&attributes_id, std::vector<storage_location_t> &&storage);
  void handle_walk_tree(std::shared_ptr<frustum_tree_walker_t> &&events);
  void handle_serialize_trees();
  void handle_trees_serialized(std::vector<tree_id_t> &&tree_ids, std::vector<storage_location_t> &&storage, error_t &&error);
  void handle_deserialize_tree(tree_id_t &&tree_id, serialized_tree_t &&data);
  void handle_request_aabb(std::function<void(double *, double *)> &&function);
  void handle_request_root();

  void seal_configuration()
  {
    std::unique_lock<std::mutex> lock(_configuration_mutex);
    if (_configuration_initialized)
      return;
    _configuration_initialized = true;
    _tree_registry.node_limit = _pre_init_node_limit;
    _tree_registry.tree_config = _pre_init_tree_config;
  }

  thread_with_event_loop_t _event_loop_thread;
  event_loop_t &_event_loop;
  std::mutex _configuration_mutex;
  bool _initialized;
  bool _configuration_initialized;
  uint32_t _pre_init_node_limit;
  tree_config_t _pre_init_tree_config;

  std::mutex _root_mutex;
  std::condition_variable _root_cv;
  bool _first_root_initialized;

  storage_handler_t &_file_cache;
  attributes_configs_t &_attributes_configs;

  tree_registry_t _tree_registry;
  std::vector<bool> _tree_id_requested;

  tree_lod_generator_t _tree_lod_generator;

  std::vector<std::function<void()>> _tree_deserialized_callbacks;

public:
  event_pipe_t<storage_header_t, attributes_id_t, std::vector<storage_location_t>> add_points;
  event_pipe_t<std::shared_ptr<frustum_tree_walker_t>> walk_tree;
  event_pipe_t<void> _serialize_trees;
  event_pipe_t<std::vector<tree_id_t>, std::vector<storage_location_t>, error_t> _serialize_trees_done;
  event_pipe_t<tree_id_t, serialized_tree_t> _deserialize_tree;
  event_pipe_t<input_data_id_t> &_done_with_input;
  event_pipe_t<std::function<void(double *, double *)>> _request_aabb;
  event_pipe_t<void> _request_root;

private:
};
} // namespace points::converter
