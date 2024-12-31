/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <uv.h>
#include <vector>

#include <points/converter/converter.h>

#include "attributes_configs.hpp"
#include "conversion_types.hpp"
#include "event_pipe.hpp"
#include "input_data_source_registry.hpp"
#include "pre_init_file_retriever.hpp"
#include "reader.hpp"
#include "storage_handler.hpp"
#include "thread_pool.hpp"
#include "threaded_event_loop.hpp"
#include "tree_handler.hpp"

namespace points::converter
{
enum class processor_open_file_semantics_t
{
  read,
  write,
  write_truncate,
};

enum class file_existence_requirement_t
{
  exist,
  not_exist,
  can_exist,
};

class frustum_tree_walker_t;
class processor_t : public about_to_block_t
{
public:
  processor_t(std::string url, file_existence_requirement_t existence_requirement, error_t &error);
  error_t upgrade_to_write(bool truncate);
  void set_pre_init_tree_config(const tree_config_t &tree_config);
  void set_pre_init_tree_node_limit(uint32_t node_limit);
  void set_runtime_callbacks(const converter_runtime_callbacks_t &runtime_callbacks, void *user_ptr);
  void set_converter_callbacks(const converter_file_convert_callbacks_t &convert_callbacks);
  void add_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&input_files);
  void walk_tree(std::shared_ptr<frustum_tree_walker_t> &&event);
  tree_config_t tree_config();
  void request_aabb(std::function<void(double[3], double[3])> callback);
  uint32_t attrib_name_registry_count();
  uint32_t attrib_name_registry_get(uint32_t index, char *name, uint32_t buffer_size);

  void wait_idle();

  void about_to_block() override;

  storage_handler_t &storage_handler()
  {
    return _storage_handler;
  }

  const attributes_t &get_attributes(attributes_id_t id);

private:
  std::string _url;
  thread_pool_t _thread_pool;
  converter_runtime_callbacks_t _runtime_callbacks;
  void *_runtime_callback_user_ptr;
  converter_file_convert_callbacks_t _convert_callbacks;

  threaded_event_loop_t _event_loop;

  bool _generating_lod;

  bool _idle;
  int _new_file_events_sent;
  std::mutex _idle_mutex;
  std::condition_variable _idle_condition;

  storage_handler_t _storage_handler;
  input_data_source_registry_t _input_data_source_registry;
  attributes_configs_t _attributes_configs;
  tree_handler_t _tree_handler;

  event_pipe_t<std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>>> _files_added;

  event_pipe_t<pre_init_info_file_result_t> _pre_init_info_file_result;
  event_pipe_t<file_error_t> _pre_init_file_errors;
  std::vector<std::unique_ptr<get_pre_init_info_worker_t>> _pre_init_info_workers;

  event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> _input_init;
  event_pipe_t<input_data_id_t> _sub_added;
  event_pipe_t<std::pair<points_t, error_t>> _sorted_points;
  event_pipe_t<file_error_t> _point_reader_file_errors;
  event_pipe_t<input_data_id_t> _point_reader_done_with_file;

  event_pipe_t<void> _storage_index_write_done;
  event_pipe_t<error_t> _storage_handler_error;
  event_pipe_t<input_data_id_t> _tree_done_with_input;

  threaded_event_loop_t _input_event_loop;
  point_reader_t _point_reader;

  int64_t _read_sort_budget;
  int64_t _read_sort_active_approximate_size;

  void handle_new_files(std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> &&new_files);
  void handle_pre_init_info_for_files(pre_init_info_file_result_t &&pre_init_info_for_files);
  void handle_file_errors_headers(file_error_t &&error);
  void handle_input_init_done(std::tuple<input_data_id_t, attributes_id_t, header_t> &&event);
  void handle_sub_added(input_data_id_t &&event);
  void handle_sorted_points(std::pair<points_t, error_t> &&event);
  void handle_file_errors(file_error_t &&error);
  void handle_file_reading_done(input_data_id_t &&file);
  void handle_index_write_done();
  void handle_storage_error(error_t &&errors);
  void handle_points_written(const storage_header_t &header, attributes_id_t attributes, std::vector<storage_location_t> &&locations);
  void handle_tree_done_with_input(input_data_id_t &&events);
};
} // namespace points::converter
