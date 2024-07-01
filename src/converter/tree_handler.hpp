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

namespace points
{
namespace converter
{
class frustum_tree_walker_t;
class tree_handler_t : public about_to_block_t
{
public:
  tree_handler_t(const tree_config_t &tree_config, storage_handler_t &file_cache, attributes_configs_t &attributrs_configs, event_pipe_t<input_data_id_t> &done_input);

  void about_to_block() override;

  void add_points(storage_header_t &&header, attributes_id_t &&attributes_id, std::vector<storage_location_t> &&storage);
  void walk_tree(std::shared_ptr<frustum_tree_walker_t> event);
  void generate_lod(const morton::morton192_t &max);

  void serialize_trees();

private:
  void handle_add_points(std::tuple<storage_header_t, attributes_id_t, std::vector<storage_location_t>> &&event);
  void handle_walk_tree(std::shared_ptr<frustum_tree_walker_t> &&events);
  void handle_serialize_trees();
  void handle_trees_serialized(std::vector<tree_id_t> &&tree_ids, std::vector<storage_location_t> &&storage, error_t &&error);
  threaded_event_loop_t _event_loop;
  bool _initialized;

  const tree_config_t &_tree_config;
  storage_handler_t &_file_cache;
  attributes_configs_t &_attributes_configs;

  tree_registry_t _tree_registry;
  tree_id_t _tree_root;

  tree_lod_generator_t _tree_lod_generator;

  event_pipe_t<std::tuple<storage_header_t, attributes_id_t, std::vector<storage_location_t>>> _add_points;
  event_pipe_t<std::shared_ptr<frustum_tree_walker_t>> _walk_tree;
  event_pipe_t<void> _serialize_trees;
  event_pipe_t<std::vector<tree_id_t>, std::vector<storage_location_t>, error_t> _serialize_trees_done;
  event_pipe_t<input_data_id_t> &_done_with_input;
};

} // namespace converter
} // namespace points
