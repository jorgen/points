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

#include "conversion_types.hpp"
#include "error.hpp"
#include "event_pipe.hpp"
#include "threaded_event_loop.hpp"
#include "worker.hpp"
#include "input_header.hpp"


namespace points
{
namespace converter
{

//class sort_worker_t : public worker_t
//{
//public:
//  sort_worker_t(const tree_global_state_t &tree_state, input_file_t &input_file, attribute_buffers_t &&buffers, uint64_t point_count, std::vector<sort_worker_t *> &done_list);
//  void work() override;
//  void after_work(completion_t completion) override;
//
//  const tree_global_state_t &tree_state;
//  input_file_t &input_file;
//  std::vector<sort_worker_t *> &done_list;
//  points_t points;
//};
//
//class batch_get_headers_t
//{
//public:
//  std::vector<get_header_worker_t> get_headers;
//  uint32_t completed = 0;
//  converter_file_convert_callbacks_t convert_callbacks;
//};
//
//struct input_file_t
//{
//  internal_header_t header;
//  void *user_ptr;
//  std::unique_ptr<get_data_worker_t> active_worker;
//  uint8_t done_read_file;
//  uint32_t sort_batches_queued;
//  uint32_t sort_batches_finished;
//};
//

struct get_points_files_t
{
  input_data_id_t id;
  input_name_ref_t filename;
  internal_header_t &header;
};

struct get_points_files_with_callbacks_t
{
  std::vector<get_points_files_t> files;
  converter_file_convert_callbacks_t &callbacks;
};

class get_data_worker_t : public worker_t
{
public:
  get_data_worker_t(const converter_file_convert_callbacks_t &convert_callbacks, get_points_files_t file);
  void work() override;
  void after_work(completion_t completion) override;

  std::unique_ptr<error_t> error;
  attribute_buffers_t buffers;
  converter_file_convert_callbacks_t convert_callbacks;
  get_points_files_t file;
  uint64_t points_read;
  bool done;
};

class point_reader_t : public about_to_block_t
{
public:
  point_reader_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<input_data_id_t> &done_with_file, event_pipe_t<file_error_t> &file_errors);
  void add_file(std::vector<get_points_files_t> &&new_files, converter_file_convert_callbacks_t &convert_callbacks);
//
  void about_to_block() override;
//
private:
  void handle_new_files(std::vector<get_points_files_with_callbacks_t> &&new_files);
//
  const tree_global_state_t &_tree_state;
  threaded_event_loop_t &_event_loop;
  event_pipe_t<points_t> &_sorted_points_pipe;
  event_pipe_t<input_data_id_t> &_done_with_file;
  event_pipe_t<file_error_t> &_file_errors;
  event_pipe_t<get_points_files_with_callbacks_t> _new_files_pipe;
  //event_pipt_t<points_read> _points_read;
  //  std::unordered_set<std::string> all_input_filenames;
  //  std::vector<std::unique_ptr<input_file_t>> input_files;
//
//  std::vector<std::unique_ptr<batch_get_headers_t>> get_headers_batch_jobs;
//
//  std::vector<get_data_worker_t *> finished_get_workers;
//
//  std::vector<std::unique_ptr<sort_worker_t>> sort_workers;
//  std::vector<sort_worker_t *> finished_sort_workers;
//
//  uint32_t active_converters;
//  uint32_t max_converters;
};
} // namespace converter
}