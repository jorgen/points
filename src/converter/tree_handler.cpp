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
#include "tree_handler.hpp"

#include "frustum_tree_walker.hpp"
#include "tree_lod_generator.hpp"

#include "storage_handler.hpp"

#include <fmt/printf.h>

namespace points
{
namespace converter
{
tree_handler_t::tree_handler_t(const tree_config_t &global_state, storage_handler_t &file_cache, attributes_configs_t &attributes_configs, event_pipe_t<input_data_id_t> &done_with_input)
  : _initialized(false)
  , _global_state(global_state)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
  , _tree_lod_generator(_event_loop, global_state, _tree_registry, _file_cache, _attributes_configs, _serialize_trees)
  , _add_points(_event_loop, bind(&tree_handler_t::handle_add_points))
  , _walk_tree(_event_loop, bind(&tree_handler_t::handle_walk_tree))
  , _serialize_trees(_event_loop, bind(&tree_handler_t::handle_serialize_trees))
  , _serialize_trees_done(_event_loop, bind(&tree_handler_t::handle_trees_serialized))
  , _done_with_input(done_with_input)
{
  _event_loop.add_about_to_block_listener(this);
}

void tree_handler_t::about_to_block()
{
}

void tree_handler_t::add_points(storage_header_t &&header, attributes_id_t &&attributes_id, std::vector<storage_location_t> &&storage)
{
  _add_points.post_event(std::make_tuple(std::move(header), std::move(attributes_id), std::move(storage)));
}

void tree_handler_t::walk_tree(std::shared_ptr<frustum_tree_walker_t> event)
{
  _walk_tree.post_event(std::move(event));
}

void tree_handler_t::handle_add_points(std::tuple<storage_header_t, attributes_id_t, std::vector<storage_location_t>> &&event)
{
  auto &&[header, attributes_id, storage] = event;
  if (!_initialized)
  {
    _initialized = true;
    _tree_root = tree_initialize(_global_state, _tree_registry, _file_cache, header, attributes_id, std::move(storage));
  }
  else
  {
    _tree_root = tree_add_points(_global_state, _tree_registry, _file_cache, _tree_root, header, attributes_id, std::move(storage));
  }
  auto to_send = header.input_id;
  _done_with_input.post_event(std::move(to_send));
}

void tree_handler_t::handle_walk_tree(std::shared_ptr<frustum_tree_walker_t> &&event)
{
  event->walk_tree(_global_state, _tree_registry, _tree_root);
}

void tree_handler_t::generate_lod(const morton::morton192_t &max)
{
  _tree_lod_generator.generate_lods(_tree_root, max);
}
void tree_handler_t::serialize_trees()
{
  _serialize_trees.post_event();
}
void tree_handler_t::handle_serialize_trees()
{
  std::vector<tree_id_t> tree_ids;
  std::vector<serialized_tree_t> serialized_trees;
  for (auto &tree : _tree_registry.data)
  {
    if (tree.is_dirty)
    {
      tree_ids.emplace_back(tree.id);
      serialized_trees.emplace_back(tree_serialize(tree));
      tree.is_dirty = false;
    }
  }
  _file_cache.write_trees(std::move(tree_ids), std::move(serialized_trees), [this](std::vector<tree_id_t> &&tree_ids, std::vector<storage_location_t> &&new_locations, error_t &&error) {
    this->_serialize_trees_done.post_event(std::move(tree_ids), std::move(new_locations), std::move(error));
  });
}
void tree_handler_t::handle_trees_serialized(std::vector<tree_id_t> &&tree_ids, std::vector<storage_location_t> &&storage, error_t &&error)
{
  (void)error;
  std::vector<storage_location_t> old_locations;
  for (int i = 0; i < int(tree_ids.size()); i++)
  {
    auto &tree_id = tree_ids[i];
    auto &location = _tree_registry.locations[tree_id.data];
    if (location.offset > 0)
    {
      old_locations.emplace_back(location);
    }
    location = storage[i];
  }
  auto serialized_registry = tree_registry_serialize(_tree_registry);
  _file_cache.write_tree_registry(std::move(serialized_registry), [this, old_locations = std::move(old_locations)](storage_location_t tree_registry_location, error_t &&error) mutable {
    (void)error;
    this->_file_cache.write_blob_locations_and_update_header(tree_registry_location, std::move(old_locations), [](error_t &&error) {
      (void)error;
      fmt::print("Trees serialized\n");
    });
  });
}

} // namespace converter
} // namespace points
