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
struct get_points_file_t
{
  input_data_id_t id;
  input_name_ref_t filename;
  converter_file_convert_callbacks_t callbacks;
};

struct point_reader_file_t;
struct unsorted_points_event_t
{
  unsorted_points_event_t(points_t &&points, point_reader_file_t &reader_file)
    : points(std::move(points))
    , reader_file(reader_file)
  {}

  points_t points;
  point_reader_file_t &reader_file;
};

class get_data_worker_t : public worker_t
{
public:
  get_data_worker_t(point_reader_file_t &point_reader_file, const get_points_file_t &file, event_pipe_t<unsorted_points_event_t> &unsorted_points_queue);
  void work() override;
  void after_work(completion_t completion) override;

  point_reader_file_t &point_reader_file;
  event_pipe_t<unsorted_points_event_t> &unsorted_points_queue;
  std::unique_ptr<error_t> error;
  get_points_file_t file;
  internal_header_t header;
  uint64_t points_read;
  uint32_t split;
};

class sort_worker_t : public worker_t
{
public:
  sort_worker_t(const tree_global_state_t &tree_state, point_reader_file_t &reader_file, points_t &&points);
  void work() override;
  void after_work(completion_t completion) override;

  const tree_global_state_t &tree_state;
  point_reader_file_t &reader_file;
  points_t points;
};

struct point_reader_file_t
{
  point_reader_file_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, const get_points_file_t &file, event_pipe_t<unsorted_points_event_t> &unsorted_points, event_pipe_t<points_t> &sorted_points_pipe)
    : tree_state(tree_state)
    , event_loop(event_loop)
    , input_reader(new get_data_worker_t(*this, file, unsorted_points))
    , sorted_points_pipe(sorted_points_pipe)
  {
    input_reader->enqueue(event_loop);
  }
  ~point_reader_file_t()
  {
  }

  const tree_global_state_t &tree_state;
  threaded_event_loop_t &event_loop;
  std::unique_ptr<get_data_worker_t> input_reader;
  std::vector<std::unique_ptr<sort_worker_t>> sort_workers;
  event_pipe_t<points_t> &sorted_points_pipe;
  uint32_t input_split = 0;
  uint32_t sort_done = 0;
};

class point_reader_t : public about_to_block_t
{
public:
  point_reader_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<input_data_id_t> &done_with_file, event_pipe_t<file_error_t> &file_errors);
  void add_file(get_points_file_t &&new_file);

  void about_to_block() override;

private:
  void handle_new_files(std::vector<get_points_file_t> &&new_files);
  void handle_unsorted_points(std::vector<unsorted_points_event_t> &&unsorted_points);

  const tree_global_state_t &_tree_state;
  threaded_event_loop_t &_event_loop;
  event_pipe_t<points_t> &_sorted_points_pipe;
  event_pipe_t<input_data_id_t> &_done_with_file;
  event_pipe_t<file_error_t> &_file_errors;
  event_pipe_t<get_points_file_t> _new_files_pipe;
  event_pipe_t<unsorted_points_event_t> _unsorted_points;
  std::vector<std::unique_ptr<point_reader_file_t>> _point_reader_files;
};
} // namespace converter
}
