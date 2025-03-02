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

#include "morton_tree_coordinate_transform.hpp"
#include "storage_handler.hpp"

namespace points::converter
{
tree_handler_t::tree_handler_t(thread_pool_t &thread_pool, storage_handler_t &file_cache, attributes_configs_t &attributes_configs, event_pipe_t<input_data_id_t> &done_with_input)
  : _event_loop_thread(thread_pool)
  , _event_loop(_event_loop_thread.event_loop())
  , _initialized(false)
  , _configuration_initialized(false)
  , _pre_init_node_limit(1000000)
  , _pre_init_tree_config({0.001, {100000, 100000, 100000}})
  , _first_root_initialized(false)
  , _file_cache(file_cache)
  , _attributes_configs(attributes_configs)
  , _tree_lod_generator(_event_loop, _tree_registry, _file_cache, _attributes_configs, _serialize_trees)
  , add_points(_event_loop, bind(&tree_handler_t::handle_add_points))
  , walk_tree(_event_loop, bind(&tree_handler_t::handle_walk_tree))
  , _serialize_trees(_event_loop, bind(&tree_handler_t::handle_serialize_trees))
  , _serialize_trees_done(_event_loop, bind(&tree_handler_t::handle_trees_serialized))
  , _deserialize_tree(_event_loop, bind(&tree_handler_t::handle_deserialize_tree))
  , _done_with_input(done_with_input)
  , _request_aabb(_event_loop, bind(&tree_handler_t::handle_request_aabb))
  , _request_root(_event_loop, bind(&tree_handler_t::handle_request_root))
{
  _event_loop.add_about_to_block_listener(this);
}

error_t tree_handler_t::deserialize_tree_registry(std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t tree_registry_blobs_size)
{
  auto ret = tree_registry_deserialize(tree_registry_buffer, tree_registry_blobs_size, _tree_registry);
  if (ret.code == 0)
  {
    _initialized = true;
    _configuration_initialized = true;
  }
  else
  {
    _tree_registry = {};
  }
  return ret;
}

void tree_handler_t::request_root()
{
  _request_root.post_event();
  std::unique_lock<std::mutex> lock(_root_mutex);
  _root_cv.wait(lock, [this] { return _first_root_initialized; });
}

void tree_handler_t::set_tree_initialization_config(const tree_config_t &config)
{
  std::unique_lock<std::mutex> lock(_configuration_mutex);
  assert(!_configuration_initialized);
  _pre_init_tree_config = config;
}

void tree_handler_t::set_tree_initialization_node_limit(uint32_t limit)
{
  std::unique_lock<std::mutex> lock(_configuration_mutex);
  assert(!_configuration_initialized);
  _pre_init_node_limit = limit;
}

void tree_handler_t::about_to_block()
{
}

void tree_handler_t::handle_add_points(storage_header_t &&header, attributes_id_t &&attributes_id, std::vector<storage_location_t> &&storage)
{
  if (!_initialized)
  {
    _initialized = true;
    seal_configuration();
    _tree_registry.root = tree_initialize(_tree_registry, _file_cache, header, attributes_id, std::move(storage));
  }
  else
  {
    _tree_registry.root = tree_add_points(_tree_registry, _file_cache, _tree_registry.root, header, attributes_id, std::move(storage));
  }
  auto to_send = header.input_id;
  _done_with_input.post_event(std::move(to_send));
}

void tree_handler_t::handle_walk_tree(std::shared_ptr<frustum_tree_walker_t> &&event)
{
  attribute_index_map_t attribute_index_map(_attributes_configs, event->m_attribute_names);
  for (auto &list : event->m_new_nodes.point_subsets)
  {
    (void)list;
  }
  tree_walk_in_handler_thread(*this, _tree_registry, attribute_index_map, *event);
}

void tree_handler_t::generate_lod(const morton::morton192_t &max)
{
  _tree_lod_generator.generate_lods(_tree_registry.root, max);
}

tree_config_t tree_handler_t::tree_config()
{
  seal_configuration();
  std::unique_lock<std::mutex> lock(_configuration_mutex);
  return _tree_registry.tree_config;
}

void tree_handler_t::request_aabb(std::function<void(double *, double *)> function)
{
  _request_aabb.post_event(std::move(function));
}

void tree_handler_t::request_tree(tree_id_t tree_id)
{
  assert(std::this_thread::get_id() == _event_loop_thread.thread_id());

  _tree_id_requested.resize(_tree_registry.data.size());
  if (_tree_id_requested[tree_id.data])
  {
    return;
  }
  _tree_id_requested[tree_id.data] = true;
  auto location = _tree_registry.locations[tree_id.data];
  _file_cache.read(location, [this, tree_id](const storage_handler_request_t &request) {
    if (request.error.code != 0)
    {
      fmt::print("Error reading tree\n");
      return;
    }
    serialized_tree_t data;
    data.size = int(request.buffer_info.size);
    data.data = request.buffer;
    this->_deserialize_tree.post_event(tree_id_t(tree_id.data), std::move(data));
  });
}

bool tree_handler_t::tree_initialized(tree_id_t tree_id)
{
  assert(std::this_thread::get_id() == _event_loop_thread.thread_id());
  return _tree_registry.tree_id_initialized[tree_id.data];
}

void tree_handler_t::handle_serialize_trees()
{
  std::vector<tree_id_t> tree_ids;
  std::vector<serialized_tree_t> serialized_trees;
  for (auto &tree : _tree_registry.data)
  {
    if (tree->is_dirty)
    {
      tree_ids.emplace_back(tree->id);
      serialized_trees.emplace_back(tree_serialize(*tree));
      if (serialized_trees.back().data == nullptr)
      {
        fmt::print(stderr, "Error serializing tree\n");
        return;
      }
      tree->is_dirty = false;
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
  _file_cache.write_tree_registry(std::move(serialized_registry), [this, old_locations_moved = std::move(old_locations)](storage_location_t tree_registry_location, error_t &&error_arg) mutable {
    (void)error_arg;
    this->_file_cache.write_blob_locations_and_update_header(tree_registry_location, std::move(old_locations_moved), [](error_t &&err) {
      (void)err;
      fmt::print("Trees serialized\n");
    });
  });
}

void tree_handler_t::handle_deserialize_tree(tree_id_t &&tree_id, serialized_tree_t &&data)
{
  assert(_tree_registry.get(tree_id) == nullptr);
  _tree_registry.data[tree_id.data] = std::make_unique<tree_t>();
  auto tree = _tree_registry.get(tree_id);
  assert(tree);
  error_t error;
  fmt::print(stderr, "Deserializing tree {}\n", tree_id.data);
  auto ret = tree_deserialize(data, *tree, error);
  if (!ret)
  {
    fmt::print("Error deserializing tree registry {}\n", error.msg);
    return;
  }
  _tree_registry.tree_id_initialized.resize(_tree_registry.data.size());
  _tree_registry.tree_id_initialized[tree_id.data] = true;
  if (tree_id.data == _tree_registry.root.data)
  {
    std::unique_lock<std::mutex> lock(_root_mutex);
    _first_root_initialized = true;
    _root_cv.notify_all();
  }
}

void tree_handler_t::handle_request_aabb(std::function<void(double *, double *)> &&function)
{
  auto tree = _tree_registry.get(_tree_registry.root);

  morton::morton192_t morton_max = {};
  morton::morton192_t morton_min = morton::morton_negate(morton_max);
  for (auto &data : tree->data[4])
  {
    if (data.min < morton_min)
    {
      morton_min = data.min;
    }
    if (morton_max < data.max)
    {
      morton_max = data.max;
    }
  }
  const auto &offset = _tree_registry.tree_config.offset;
  const auto &scale = _tree_registry.tree_config.scale;
  double min[3];
  double max[3];
  convert_morton_to_pos(scale, offset, morton_min, min);
  convert_morton_to_pos(scale, offset, morton_max, max);
  function(min, max);
}

void tree_handler_t::handle_request_root()
{
  request_tree(_tree_registry.root);
}
} // namespace points::converter
