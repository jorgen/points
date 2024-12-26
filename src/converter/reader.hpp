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

#include <fmt/printf.h>

#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <condition_variable>
#include <functional>
#include <points/converter/converter.h>

#include "attributes_configs.hpp"
#include "conversion_types.hpp"
#include "error.hpp"
#include "event_pipe.hpp"
#include "threaded_event_loop.hpp"
#include "worker.hpp"


namespace points::converter
{
class storage_handler_t;
struct get_points_file_t
{
  input_data_id_t id;
  input_name_ref_t filename;
  converter_file_convert_callbacks_t callbacks;
};

struct point_reader_file_t;
struct unsorted_points_event_t
{
  unsorted_points_event_t(std::vector<point_format_t> attributes_def, const header_t &public_header, points_t &&points, point_reader_file_t &reader_file)
    : attributes_def(attributes_def)
    , public_header(public_header)
    , points(std::move(points))
    , reader_file(reader_file)
  {
  }

  std::vector<point_format_t> attributes_def;
  header_t public_header;
  points_t points;
  point_reader_file_t &reader_file;
};

class get_data_worker_t : public worker_t
{
public:
  get_data_worker_t(point_reader_file_t &point_reader_file, attributes_configs_t &attribute_configs, const get_points_file_t &file, event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe,
                    event_pipe_t<input_data_id_t> &sub_added, event_pipe_t<unsorted_points_event_t> &unsorted_points_queue);
  void work() override;
  void after_work(completion_t completion) override;

  point_reader_file_t &point_reader_file;
  attributes_configs_t &attribute_configs;
  event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe;
  event_pipe_t<input_data_id_t> &sub_added;
  event_pipe_t<unsorted_points_event_t> &unsorted_points_queue;
  std::unique_ptr<error_t> error;
  get_points_file_t file;
  storage_header_t storage_header;
  uint64_t points_read;
  uint32_t split;
};

class sort_worker_t : public worker_t
{
public:
  sort_worker_t(const tree_config_t &tree_config, point_reader_file_t &reader_file, attributes_configs_t &attributes_configs, header_t public_header, points_t &&points);
  void work() override;
  void after_work(completion_t completion) override;

  tree_config_t _tree_config;
  point_reader_file_t &reader_file;
  attributes_configs_t &attributes_configs;
  header_t public_header;
  points_t points;
  error_t error;
};

struct point_reader_file_t
{
  point_reader_file_t(const tree_config_t &tree_config, threaded_event_loop_t &event_loop, attributes_configs_t &attributes_configs, const get_points_file_t &file,
                      event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe, event_pipe_t<input_data_id_t> &sub_added, event_pipe_t<unsorted_points_event_t> &unsorted_points,
                      event_pipe_t<std::pair<points_t, error_t>> &sorted_points_pipe)
    : tree_config(tree_config)
    , event_loop(event_loop)
    , input_reader(new get_data_worker_t(*this, attributes_configs, file, input_init_pipe, sub_added, unsorted_points))
    , sorted_points_pipe(sorted_points_pipe)
  {
    input_reader->enqueue(event_loop);
  }
  ~point_reader_file_t()
  {
  }

  tree_config_t tree_config;
  threaded_event_loop_t &event_loop;
  std::unique_ptr<get_data_worker_t> input_reader;
  std::vector<std::unique_ptr<sort_worker_t>> sort_workers;
  event_pipe_t<std::pair<points_t, error_t>> &sorted_points_pipe;
  uint32_t input_split = 0;
  uint32_t sort_done = 0;
};

class memory_requester_t
{
  memory_requester_t(uint64_t available_memory)
    : _available_memory(available_memory)
  {
  }

  void request_memory(uint64_t memory)
  {
    std::unique_lock<std::mutex> lock(_mutex);
    uint64_t acquired_memory = 0;

    _wait_condition.wait(lock, [memory, &acquired_memory, this]() {
      acquired_memory += std::min(memory - acquired_memory, _available_memory);
      return acquired_memory == memory;
    });
    return;
  }

  void release_memory(uint64_t memory)
  {
    std::unique_lock<std::mutex> lock(_mutex);
    _available_memory += memory;
    _wait_condition.notify_all();
  }

private:
  uint64_t _available_memory;
  std::mutex _mutex;
  std::condition_variable _wait_condition;
};

class point_reader_t : public about_to_block_t
{
public:
  point_reader_t(threaded_event_loop_t &event_loop, attributes_configs_t &attributes_configs, event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe,
                 event_pipe_t<input_data_id_t> &sub_added, event_pipe_t<std::pair<points_t, error_t>> &sorted_points_pipe, event_pipe_t<input_data_id_t> &done_with_file, event_pipe_t<file_error_t> &file_errors);
  void add_file(tree_config_t tree_config, get_points_file_t &&new_file);

  void about_to_block() override;

private:
  void handle_new_files(tree_config_t &&tree_config, get_points_file_t &&new_file);
  void handle_unsorted_points(unsorted_points_event_t &&unsorted_points);

  threaded_event_loop_t &_event_loop;
  attributes_configs_t &_attributes_configs;
  event_pipe_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &_input_init_pipe;
  event_pipe_t<input_data_id_t> &_sub_added;
  event_pipe_t<std::pair<points_t, error_t>> &_sorted_points_pipe;
  event_pipe_t<input_data_id_t> &_done_with_file;
  event_pipe_t<file_error_t> &_file_errors;
  event_pipe_t<tree_config_t, get_points_file_t> _new_files_pipe;
  event_pipe_t<unsorted_points_event_t> _unsorted_points;
  std::vector<std::unique_ptr<point_reader_file_t>> _point_reader_files;
};
} // namespace points::converter

