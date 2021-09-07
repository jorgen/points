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

#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_set>

#include <points/converter/converter.h>
#include <functional>

#include "conversion_types_p.h"
#include "error_p.h"
#include "event_pipe_p.h"
#include "threaded_event_loop_p.h"
#include "worker_p.h"
#include "input_header_p.h"

namespace points
{
namespace converter
{
struct input_files
{
  std::string name;
  uint64_t size;
};

class batch_get_headers_t;
class get_header_worker_t : public worker_t
{
public:
  get_header_worker_t(batch_get_headers_t &batch_get_headers, const std::string file_name, bool close);
  void work() override;
  void after_work(completion_t completion) override;

  batch_get_headers_t &batch;
  std::string file_name;
  internal_header_t header;
  bool close;
  std::unique_ptr<error_t> error;
  void *user_ptr;
};

struct input_file_t;
class get_data_worker_t : public worker_t
{
public:
  get_data_worker_t(converter_file_convert_callbacks_t &convert_callbacks, input_file_t &input_file, std::vector<get_data_worker_t *> &done_list);
  void work() override;
  void after_work(completion_t completion) override;

  std::unique_ptr<error_t> error;
  attribute_buffers_t buffers;
  converter_file_convert_callbacks_t &convert_callbacks;
  input_file_t &input_file;
  uint64_t points_read;
  std::vector<get_data_worker_t *> &done_list;
};

class sort_worker_t : public worker_t
{
public:
  sort_worker_t(const tree_global_state_t &tree_state, const internal_header_t &header, attribute_buffers_t &&buffers, uint64_t point_count, std::vector<sort_worker_t *> &done_list);
  void work() override;
  void after_work(completion_t completion) override;

  const tree_global_state_t &tree_state;
  std::vector<sort_worker_t *> &done_list;
  points_t points;
};

class batch_get_headers_t
{
public:
  std::vector<get_header_worker_t> get_headers;
  uint32_t completed = 0;
  converter_file_convert_callbacks_t convert_callbacks;
};

struct input_file_t
{
  internal_header_t header;
  void *user_ptr;
  std::unique_ptr<get_data_worker_t> active_worker;
  uint8_t done;
};

class point_reader_t : public about_to_block_t
{
public:
  point_reader_t(const tree_global_state_t &tree_state, event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<file_error_t> &file_errors, converter_file_convert_callbacks_t &convert_callbacks);
  void add_files(const std::vector<std::string> &files);
  // void add_data(const void *data, size_t data_size);

  void about_to_block() override;

private:
  void handle_new_files(std::vector<std::vector<std::string>> &&new_files);

  const tree_global_state_t &tree_state;

  threaded_event_loop_t event_loop;
  event_pipe_t<points_t> &sorted_points_pipe;
  event_pipe_t<file_error_t> &file_errors;
  event_pipe_t<std::vector<std::string>> new_files_pipe;
  
  converter_file_convert_callbacks_t &convert_callbacks;

  std::unordered_set<std::string> all_input_filenames;
  std::vector<std::unique_ptr<input_file_t>> input_files;

  std::vector<std::unique_ptr<batch_get_headers_t>> get_headers_batch_jobs;

  std::vector<get_data_worker_t *> finished_get_workers;

  std::vector<std::unique_ptr<sort_worker_t>> sort_workers;
  std::vector<sort_worker_t *> finished_sort_workers;

  uint32_t active_converters;
  uint32_t max_converters;
};
} // namespace converter
}
