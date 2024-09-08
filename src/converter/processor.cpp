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

#include "threaded_event_loop.hpp"

#include "morton_tree_coordinate_transform.hpp"

#include <algorithm>
#include <functional>
#include <stdlib.h>

namespace points
{
namespace converter
{

processor_t::processor_t(std::string url, error_t &error)
  : _url(std::move(url))
  , _thread_pool(int(std::thread::hardware_concurrency()))
  , _runtime_callbacks({})
  , _runtime_callback_user_ptr(nullptr)
  , _convert_callbacks({})
  , _event_loop(_thread_pool)
  , _generating_lod(false)
  , _idle(true)
  , _new_file_events_sent(0)
  , _storage_handler(_url, _thread_pool, _attributes_configs, _storage_index_write_done, _storage_handler_error, error)
  , _tree_handler(_thread_pool, _storage_handler, _attributes_configs, _tree_done_with_input)
  , _files_added(_event_loop, bind(&processor_t::handle_new_files))
  , _pre_init_info_file_result(_event_loop, bind(&processor_t::handle_pre_init_info_for_files))
  , _pre_init_file_errors(_event_loop, bind(&processor_t::handle_file_errors_headers))
  , _input_init(_event_loop, bind(&processor_t::handle_input_init_done))
  , _sub_added(_event_loop, bind(&processor_t::handle_sub_added))
  , _sorted_points(_event_loop, bind(&processor_t::handle_sorted_points))
  , _point_reader_file_errors(_event_loop, bind(&processor_t::handle_file_errors))
  , _point_reader_done_with_file(_event_loop, bind(&processor_t::handle_file_reading_done))
  , _storage_index_write_done(_event_loop, bind(&processor_t::handle_index_write_done))
  , _storage_handler_error(_event_loop, bind(&processor_t::handle_storage_error))
  , _tree_done_with_input(_event_loop, bind(&processor_t::handle_tree_done_with_input))
  , _input_event_loop(_thread_pool)
  , _point_reader(_input_event_loop, _attributes_configs, _input_init, _sub_added, _sorted_points, _point_reader_done_with_file, _point_reader_file_errors)
  , _read_sort_budget(uint64_t(1) << 20)
  , _read_sort_active_approximate_size(0)
{
  _event_loop.add_about_to_block_listener(this);

  if (error.code != 0)
    return;

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

void processor_t::walk_tree(const std::shared_ptr<frustum_tree_walker_t> &event)
{
  _tree_handler.walk_tree(event);
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
  auto tree_config = _tree_handler.tree_config();
  for (auto &new_file : new_files)
  {
    auto input_ref = _input_data_source_registry.register_file(std::move(new_file.first), new_file.second);
    _pre_init_info_workers.emplace_back(new get_pre_init_info_worker_t(tree_config, input_ref.input_id, input_ref.name, _convert_callbacks, _pre_init_info_file_result, _pre_init_file_errors));
    _pre_init_info_workers.back()->enqueue(_event_loop);
  }
  std::unique_lock<std::mutex> lock(_idle_mutex);
  _new_file_events_sent--;
}

void processor_t::handle_pre_init_info_for_files(pre_init_info_file_result_t &&pre_init_for_file)
{
  assert(std::count_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == pre_init_for_file.id; }) == 1);
  _pre_init_info_workers.erase(std::find_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == pre_init_for_file.id; }));
  _input_data_source_registry.register_pre_init_result(_tree_handler.tree_config(), pre_init_for_file.id, pre_init_for_file.found_min, pre_init_for_file.min, pre_init_for_file.approximate_point_count,
                                                       pre_init_for_file.approximate_point_size_bytes);
}

void processor_t::handle_file_errors_headers(file_error_t &&error)
{
  assert(std::count_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == error.input_id; }) == 1);
  _pre_init_info_workers.erase(std::find_if(_pre_init_info_workers.begin(), _pre_init_info_workers.end(), [&](std::unique_ptr<get_pre_init_info_worker_t> &worker) { return worker->input_id == error.input_id; }));
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
    [this](const storage_header_t &header, attributes_id_t attributes, std::vector<storage_location_t> &&locations, const error_t &) { this->handle_points_written(header, attributes, std::move(locations)); });
}

void processor_t::handle_file_errors(file_error_t &&errors)
{
  (void)errors;
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

void processor_t::handle_storage_error(error_t &&errors)
{
  fmt::print(stderr, "File error {} {}\n", errors.code, errors.msg);
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

} // namespace converter
} // namespace points
