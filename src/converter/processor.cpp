/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#include "processor.hpp"

#include "conversion_types.hpp"
#include "converter.hpp"
#include "frustum_tree_walker.hpp"

#include "morton_tree_coordinate_transform.hpp"

#include <algorithm>
#include <functional>

namespace points::converter
{
processor_t::processor_t(std::string url, file_existence_requirement_t existence_requirement, error_t &error)
  : _url(std::move(url))
  , _thread_pool(int(std::thread::hardware_concurrency()))
  , _runtime_callbacks({})
  , _runtime_callback_user_ptr(nullptr)
  , _convert_callbacks({})
  , _thread_with_event_loop()
  , _event_loop(_thread_with_event_loop.event_loop())
  , _generating_lod(false)
  , _has_errors(false)
  , _idle(true)
  , _new_file_events_sent(0)
  , _storage_handler(_url, _thread_pool, _attributes_configs, _storage_index_write_done, _storage_handler_error, error)
  , _tree_handler(_thread_pool, _storage_handler, _attributes_configs, _tree_done_with_input)
  , _files_added(_event_loop, bind(&processor_t::handle_new_files))
  , _input_init(_event_loop, bind(&processor_t::handle_input_init_done))
  , _sub_added(_event_loop, bind(&processor_t::handle_sub_added))
  , _sorted_points(_event_loop, bind(&processor_t::handle_sorted_points))
  , _point_reader_file_errors(_event_loop, bind(&processor_t::handle_file_errors))
  , _point_reader_done_with_file(_event_loop, bind(&processor_t::handle_file_reading_done))
  , _storage_index_write_done(_event_loop, bind(&processor_t::handle_index_write_done))
  , _storage_handler_error(_event_loop, bind(&processor_t::handle_storage_error))
  , _tree_done_with_input(_event_loop, bind(&processor_t::handle_tree_done_with_input))
  , _input_event_loop_thread()
  , _input_event_loop(_input_event_loop_thread.event_loop())
  , _point_reader(_input_event_loop, _thread_pool, _attributes_configs, _input_init, _sub_added, _sorted_points, _point_reader_done_with_file, _point_reader_file_errors)
  , _read_sort_budget(uint64_t(1) << 30)
  , _read_sort_active_approximate_size(0)
{
  _event_loop.add_about_to_block_listener(this);

  if (error.code != 0)
    return;

  if (existence_requirement == file_existence_requirement_t::exist)
  {
    if (!_storage_handler.file_exists())
    {
      error = {1, "File does not exist"};
      return;
    }
  }
  else if (existence_requirement == file_existence_requirement_t::not_exist)
  {
    if (_storage_handler.file_exists())
    {
      error = {1, "File exists"};
      return;
    }
  }
  if (_storage_handler.file_exists())
  {
    std::unique_ptr<uint8_t[]> free_blobs_buffer;
    uint32_t free_blobs_buffer_size = 0;
    std::unique_ptr<uint8_t[]> attribute_blobs_buffer;
    uint32_t attribute_blobs_buffer_size = 0;
    std::unique_ptr<uint8_t[]> tree_registry_buffer;
    uint32_t tree_registry_blobs_size = 0;
    error = _storage_handler.read_index(free_blobs_buffer, free_blobs_buffer_size, attribute_blobs_buffer, attribute_blobs_buffer_size, tree_registry_buffer, tree_registry_blobs_size);
    if (error.code != 0)
      return;
    error = _storage_handler.deserialize_free_blobs(free_blobs_buffer, free_blobs_buffer_size);
    if (error.code != 0)
      return;
    error = _attributes_configs.deserialize(attribute_blobs_buffer, attribute_blobs_buffer_size);
    if (error.code != 0)
      return;
    error = _tree_handler.deserialize_tree_registry(tree_registry_buffer, tree_registry_blobs_size);
    if (error.code != 0)
      return;
    _tree_handler.request_root();
  }
}

void processor_t::add_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&input_files)
{
  {
    std::unique_lock<std::mutex> lock(_idle_mutex);
    _idle = false;
    _new_file_events_sent++;
  }
  _files_added.post_event(std::move(input_files));
}

void processor_t::walk_tree(frustum_tree_walker_t &walker)
{
  if (!_attribute_index_map || _cached_attribute_names != walker.m_attribute_names)
  {
    _attribute_index_map = std::make_unique<attribute_index_map_t>(_tree_handler.attributes_configs(), walker.m_attribute_names);
    _cached_attribute_names = walker.m_attribute_names;
  }
  walk_tree_direct(_tree_handler.tree_registry(), *_attribute_index_map, walker);
  _tree_handler.request_trees_async(std::move(walker.m_trees_to_load));
}

tree_config_t processor_t::tree_config()
{
  return _tree_handler.tree_config();
}

void processor_t::request_aabb(std::function<void(double[3], double[3])> callback)
{
  _tree_handler.request_aabb(callback);
}

void processor_t::wait_idle()
{
  std::unique_lock<std::mutex> lock(_idle_mutex);
  _idle_condition.wait(lock, [this] { return _idle; });
}

void processor_t::about_to_block()
{
  while (_read_sort_budget - _read_sort_active_approximate_size > 0)
  {
    auto next_input = _input_data_source_registry.next_input_to_process();
    if (!next_input)
      break;
    _read_sort_active_approximate_size += next_input->approximate_point_count * next_input->approximate_point_size_bytes;
    get_points_file_t file;
    file.callbacks = _convert_callbacks;
    file.id = next_input->id;
    file.filename = next_input->name;
    _point_reader.add_file(_tree_handler.tree_config(), std::move(file));
  }
  std::unique_lock<std::mutex> lock(_idle_mutex);
  if (_input_data_source_registry.all_inserted_into_tree() && _new_file_events_sent == 0 && !_generating_lod)
  {
    _idle = true;
    _idle_condition.notify_all();
  }
}

const attributes_t &processor_t::get_attributes(attributes_id_t id)
{
  return _attributes_configs.get(id);
}

void processor_t::handle_new_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&new_files)
{
  auto tree_config_val = _tree_handler.tree_config();
  std::vector<std::pair<input_data_id_t, input_name_ref_t>> file_refs;
  file_refs.reserve(new_files.size());
  for (auto &new_file : new_files)
  {
    auto input_ref = _input_data_source_registry.register_file(std::move(new_file.first), new_file.second);
    file_refs.emplace_back(input_ref.input_id, input_ref.name);
  }
  {
    std::unique_lock<std::mutex> lock(_idle_mutex);
    _new_file_events_sent--;
  }

  // Launch pre-init processing as a detached coroutine using schedule_work
  [](processor_t *self, std::vector<std::pair<input_data_id_t, input_name_ref_t>> refs, tree_config_t tc) -> vio::detached_task_t
  {
    co_await self->do_handle_new_files(std::move(refs), std::move(tc));
  }(this, std::move(file_refs), std::move(tree_config_val));
}

struct pre_init_work_result_t
{
  input_data_id_t input_id;
  pre_init_info_file_result_t pre_init_result;
  file_error_t file_error;
  bool has_error = false;
};

vio::task_t<void> processor_t::do_handle_new_files(std::vector<std::pair<input_data_id_t, input_name_ref_t>> file_refs, tree_config_t tree_config_val)
{
  std::vector<std::function<std::expected<pre_init_work_result_t, vio::error_t>()>> work_items;
  work_items.reserve(file_refs.size());

  for (auto &[input_id, file_name] : file_refs)
  {
    auto *callbacks = &_convert_callbacks;
    work_items.push_back([input_id, file_name, callbacks]() -> std::expected<pre_init_work_result_t, vio::error_t>
    {
      pre_init_work_result_t result;
      result.input_id = input_id;

      error_t *local_error = nullptr;
      auto pre_init_info = callbacks->pre_init(file_name.name, file_name.name_length, &local_error);
      if (local_error)
      {
        std::unique_ptr<error_t> error(local_error);
        result.has_error = true;
        result.file_error.input_id = input_id;
        result.file_error.error = std::move(*error);
      }
      else
      {
        result.pre_init_result.id = input_id;
        result.pre_init_result.found_min = pre_init_info.found_aabb_min;
        memcpy(result.pre_init_result.min, pre_init_info.aabb_min, sizeof(result.pre_init_result.min));
        result.pre_init_result.approximate_point_count = pre_init_info.approximate_point_count;
        result.pre_init_result.approximate_point_size_bytes = pre_init_info.approximate_point_size_bytes;
      }
      return result;
    });
  }

  auto results = co_await vio::schedule_work(_event_loop, _thread_pool, std::move(work_items));

  // Process results on the event loop thread
  for (auto &result : results)
  {
    if (!result.has_value())
      continue;
    auto &r = result.value();
    if (r.has_error)
    {
      _input_data_source_registry.handle_file_failed(r.input_id);
      _has_errors = true;
      if (_runtime_callbacks.error)
        _runtime_callbacks.error(_runtime_callback_user_ptr, &r.file_error.error);
    }
    else
    {
      _input_data_source_registry.register_pre_init_result(_tree_handler.tree_config(), r.pre_init_result.id, r.pre_init_result.found_min, r.pre_init_result.min,
                                                           r.pre_init_result.approximate_point_count, r.pre_init_result.approximate_point_size_bytes);
    }
  }
}

void processor_t::handle_input_init_done(std::tuple<input_data_id_t, attributes_id_t, header_t> &&event)
{
  _input_data_source_registry.handle_input_init(std::get<0>(event), std::get<1>(event), std::get<2>(event));
}

void processor_t::handle_sub_added(input_data_id_t &&event)
{
  _input_data_source_registry.handle_sub_added(event);
}

void processor_t::handle_sorted_points(std::pair<points_t, error_t> &&event)
{
  _input_data_source_registry.handle_sorted_points(event.first.header.input_id, event.first.header.morton_min, event.first.header.morton_max);
  _storage_handler.write(
    event.first.header, event.first.attributes_id, std::move(event.first.buffers),
    [this](const storage_header_t &header, attributes_id_t attributes, std::vector<storage_location_t> locations, const error_t &) { this->handle_points_written(header, attributes, std::move(locations)); });
}

void processor_t::handle_file_errors(file_error_t &&error)
{
  _has_errors = true;
  if (_runtime_callbacks.error)
    _runtime_callbacks.error(_runtime_callback_user_ptr, &error.error);
}

void processor_t::handle_file_reading_done(input_data_id_t &&file)
{
  _input_data_source_registry.handle_reading_done(file);
}

void processor_t::handle_index_write_done()
{
  _generating_lod = false;
  fwrite(fmt::format("index write done\n").c_str(), 1, 9, stderr);
  if (_runtime_callbacks.done)
  {
    _runtime_callbacks.done(_runtime_callback_user_ptr);
  }
}

void processor_t::handle_storage_error(error_t &&error)
{
  _has_errors = true;
  if (_runtime_callbacks.error)
    _runtime_callbacks.error(_runtime_callback_user_ptr, &error);
}

void processor_t::handle_points_written(const storage_header_t &header, attributes_id_t attributes_id, std::vector<storage_location_t> &&locations)
{
  if (input_data_id_is_leaf(header.input_id))
  {
    auto locations_copy = locations;
    _input_data_source_registry.handle_points_written(header.input_id, std::move(locations));
    storage_header_t header_copy(header);
    _tree_handler.add_points(std::move(header_copy), std::move(attributes_id), std::move(locations_copy));
  }
}

void processor_t::handle_tree_done_with_input(input_data_id_t &&event)
{
  _input_data_source_registry.handle_tree_done_with_input(event);
  if (_generating_lod)
    return;
  auto min = _input_data_source_registry.get_done_morton();
  if (min)
  {
    _generating_lod = true;
    _tree_handler.generate_lod(*min);
  }
}

error_t processor_t::upgrade_to_write(bool truncate)
{
  auto ret = _storage_handler.upgrade_to_write(truncate);
  if (truncate)
  {
    //_input_data_source_registry.~input_data_source_registry_t();
    // new (&_input_data_source_registry) input_data_source_registry_t();

    //_attributes_configs.~attributes_configs_t();
    // new (&_attributes_configs) attributes_configs_t();

    //_tree_handler.~tree_handler_t();
    // new (&_tree_handler) tree_handler_t(_thread_pool, _storage_handler, _attributes_configs, _tree_done_with_input);
  }
  return ret;
}

void processor_t::set_pre_init_tree_config(const tree_config_t &tree_config)
{
  _tree_handler.set_tree_initialization_config(tree_config);
}

void processor_t::set_pre_init_tree_node_limit(uint32_t node_limit)
{
  _tree_handler.set_tree_initialization_node_limit(node_limit);
}

void processor_t::set_runtime_callbacks(const converter_runtime_callbacks_t &runtime_callbacks, void *user_ptr)
{
  _runtime_callbacks = runtime_callbacks;
  _runtime_callback_user_ptr = user_ptr;
}

void processor_t::set_converter_callbacks(const converter_file_convert_callbacks_t &convert_callbacks)
{
  _convert_callbacks = convert_callbacks;
}

uint32_t processor_t::attrib_name_registry_count()
{
  return _attributes_configs.attrib_name_registry_count();
}

uint32_t processor_t::attrib_name_registry_get(uint32_t index, char *name, uint32_t buffer_size)
{
  return _attributes_configs.attrib_name_registry_get(index, name, buffer_size);
}
} // namespace points::converter
