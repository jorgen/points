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
#include "reader.hpp"

#include "threaded_event_loop.hpp"
#include "morton.hpp"
#include "sorter.hpp"
#include "morton_tree_coordinate_transform.hpp"

#include <fmt/printf.h>

#include <points/converter/default_attribute_names.h>

#include <assert.h>

namespace points
{
namespace converter
{
get_data_worker_t::get_data_worker_t(point_reader_file_t &point_reader_file, const get_points_file_t &file, event_pipe_t<unsorted_points_event_t> &unsorted_points_queue)
  : point_reader_file(point_reader_file)
  , unsorted_points_queue(unsorted_points_queue)
  , file(file)
  , points_read(0)
  , split(0)
{
}

struct callback_closer
{
  callback_closer(converter_file_convert_callbacks_t &callbacks, void *user_ptr)
    : callbacks(callbacks)
    , user_ptr(user_ptr)
  {}
  ~callback_closer()
  {
    if (callbacks.destroy_user_ptr && user_ptr)
    {
      callbacks.destroy_user_ptr(user_ptr);
    }
  }

  converter_file_convert_callbacks_t &callbacks;
  void *user_ptr;
};

void get_data_worker_t::work()
{
  internal_header_initialize(header);
  error_t *local_error = nullptr;
  void *user_ptr;
  file.callbacks.init(file.filename.name, file.filename.name_length, &header, &user_ptr, &local_error);
  callback_closer closer(file.callbacks, user_ptr);
  if (local_error)
  {
    error.reset(local_error);
    return;
  }

  int convert_size = 20000;
  uint8_t done_read_file = false;
  uint64_t local_points_read;
  while(!done_read_file)
  {
    points_t points;
    attribute_buffers_t buffers;
    attribute_buffers_initialize(header.attributes.attributes, buffers, convert_size);
    file.callbacks.convert_data(user_ptr, &header, header.attributes.attributes.data(), header.attributes.attributes.size(), convert_size, buffers.buffers.data(), buffers.buffers.size(), &local_points_read, &done_read_file, &local_error);
    if (local_error)
    {
      error.reset(local_error);
      return;
    }
    attribute_buffers_adjust_buffers_to_size(header.attributes.attributes, buffers, points_read);
    points_read += local_points_read;
    split++;
    unsorted_points_event_t event(std::move(points), point_reader_file);
    unsorted_points_queue.post_event(std::move(event));
  }
}

void get_data_worker_t::after_work(completion_t completion)
{
  (void)completion;
  point_reader_file.input_split = split;
}
  
sort_worker_t::sort_worker_t(const tree_global_state_t &tree_state, point_reader_file_t &reader_file, points_t &&points)
  : tree_state(tree_state)
  , reader_file(reader_file)
  , points(std::move(points))
{
}

void sort_worker_t::work()
{
  sort_points(tree_state, points);
}

void sort_worker_t::after_work(completion_t completion)
{
  (void)completion;
  reader_file.sort_done++;
  reader_file.sorted_points_pipe.post_event(std::move(points));
}

point_reader_t::point_reader_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<input_data_id_t> &done_with_file, event_pipe_t<file_error_t> &file_errors)
  : _tree_state(tree_state)
  , _event_loop(event_loop)
  , _sorted_points_pipe(sorted_points_pipe)
  , _done_with_file(done_with_file)
  , _file_errors(file_errors)
  , _new_files_pipe(event_loop, [this](std::vector<get_points_file_t> &&new_files) { handle_new_files(std::move(new_files));})
  , _unsorted_points(event_loop, [this](std::vector<unsorted_points_event_t> &&unsorted_points) { handle_unsorted_points(std::move(unsorted_points));})
{
(void) _file_errors;
  event_loop.add_about_to_block_listener(this);
}

void point_reader_t::add_file(get_points_file_t &&new_file)
{
  _new_files_pipe.post_event(std::move(new_file));
}

void point_reader_t::about_to_block()
{
  auto finished = std::partition(_point_reader_files.begin(), _point_reader_files.end(), [](const std::unique_ptr<point_reader_file_t> &a) { return a->input_split == a->sort_done;});
  for (auto it = finished; it != _point_reader_files.end(); ++it)
  {
    _done_with_file.post_event(it->get()->input_reader->header.input_id);
  }
}

void point_reader_t::handle_new_files(std::vector<get_points_file_t> &&new_files)
{
  for (auto &new_file : new_files)
  {
    _point_reader_files.emplace_back(new point_reader_file_t(_tree_state, _event_loop, new_file, _unsorted_points, _sorted_points_pipe));
  }
}

void point_reader_t::handle_unsorted_points(std::vector<unsorted_points_event_t> &&unsorted_points)
{
  for (auto &unsorted_point_event : unsorted_points)
  {
    auto &tree_state = unsorted_point_event.reader_file.tree_state;
    auto &reader_file = unsorted_point_event.reader_file;
    unsorted_point_event.reader_file.sort_workers.emplace_back(new sort_worker_t(tree_state, reader_file, std::move(unsorted_point_event.points)));
  }
}

  
}
} // namespace points
