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

#include "input_header.hpp"
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
get_data_worker_t::get_data_worker_t(point_reader_file_t &point_reader_file, attributes_configs_t &attribute_configs, const get_points_file_t &file, event_pipe_single_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe, event_pipe_single_t<input_data_id_t> &sub_added, event_pipe_single_t<unsorted_points_event_t> &unsorted_points_queue)
  : point_reader_file(point_reader_file)
  , attribute_configs(attribute_configs)
  , input_init_pipe(input_init_pipe)
  , sub_added(sub_added)
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
  storage_header_initialize(storage_header);
  attributes_t tmp_attributes;
  error_t *local_error = nullptr;
  void *user_ptr;
  header_t public_header;
  file.callbacks.init(file.filename.name, file.filename.name_length, &public_header, &tmp_attributes, &user_ptr, &local_error);
  callback_closer closer(file.callbacks, user_ptr);
  if (local_error)
  {
    error.reset(local_error);
    return;
  }

  if (tmp_attributes.attributes[0].name_size != strlen(POINTS_ATTRIBUTE_XYZ)
      || memcmp(tmp_attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ, tmp_attributes.attributes[0].name_size) != 0)
  {
    error.reset(new error_t());
    error->code = -1;
    error->msg = "First attribute has to be " POINTS_ATTRIBUTE_XYZ;
    return;
  }

  storage_header.input_id = file.id;
  storage_header.attributes_id = attribute_configs.get_attribute_config_index(std::move(tmp_attributes));
  auto &attributes = attribute_configs.get(storage_header.attributes_id);
  auto attribute_info = attribute_configs.get_format_components(storage_header.attributes_id);
  input_init_pipe.post_event(std::make_tuple(storage_header.input_id, storage_header.attributes_id, public_header));

  int convert_size = 20000;
  uint8_t done_read_file = false;
  uint64_t local_points_read;
  uint32_t sub_part = 0;
  while(!done_read_file)
  {
    points_t points;
    points.header = storage_header;
    points.header.input_id.sub = sub_part++;
    points.header.point_count = convert_size;
    attribute_buffers_initialize(attribute_info, points.buffers, convert_size);
    file.callbacks.convert_data(user_ptr, &public_header, attributes.attributes.data(), attributes.attributes.size(), convert_size, points.buffers.buffers.data(), points.buffers.buffers.size(), &local_points_read, &done_read_file, &local_error);
    if (local_error)
    {
      error.reset(local_error);
      return;
    }
    sub_added.post_event(points.header.input_id);
    attribute_buffers_adjust_buffers_to_size(attribute_info, points.buffers, local_points_read);
    points_read += local_points_read;
    points.header.point_count = local_points_read;
    split++;
    unsorted_points_event_t event(attribute_info, public_header, std::move(points), point_reader_file);
    unsorted_points_queue.post_event(std::move(event));
  }
}

void get_data_worker_t::after_work(completion_t completion)
{
  (void)completion;
  point_reader_file.input_split = split;
}
  
sort_worker_t::sort_worker_t(const tree_global_state_t &tree_state, point_reader_file_t &reader_file, attributes_configs_t &attributes_configs, header_t public_header, points_t &&points)
  : tree_state(tree_state)
  , reader_file(reader_file)
  , attributes_configs(attributes_configs)
  , public_header(public_header)
  , points(std::move(points))
{
}

void sort_worker_t::work()
{
  sort_points(tree_state, attributes_configs, public_header, points, error);
}

void sort_worker_t::after_work(completion_t completion)
{
  (void)completion;
  reader_file.sort_done++;
  reader_file.sorted_points_pipe.post_event(std::make_pair(std::move(points), std::move(error)));
}

point_reader_t::point_reader_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, attributes_configs_t &attributes_configs, event_pipe_single_t<std::tuple<input_data_id_t, attributes_id_t, header_t>> &input_init_pipe, event_pipe_single_t<input_data_id_t> &sub_added, event_pipe_single_t<std::pair<points_t,error_t>> &sorted_points_pipe, event_pipe_single_t<input_data_id_t> &done_with_file, event_pipe_single_t<file_error_t> &file_errors)
  : _tree_state(tree_state)
  , _event_loop(event_loop)
  , _attributes_configs(attributes_configs)
  , _input_init_pipe(input_init_pipe)
  , _sub_added(sub_added)
  , _sorted_points_pipe(sorted_points_pipe)
  , _done_with_file(done_with_file)
  , _file_errors(file_errors)
  , _new_files_pipe(event_loop, bind(&point_reader_t::handle_new_files))
  , _unsorted_points(event_loop, bind(&point_reader_t::handle_unsorted_points))
{
  event_loop.add_about_to_block_listener(this);
}

void point_reader_t::add_file(get_points_file_t &&new_file)
{
  _new_files_pipe.post_event(std::move(new_file));
}

void point_reader_t::about_to_block()
{
  auto finished = std::partition(_point_reader_files.begin(), _point_reader_files.end(), [](const std::unique_ptr<point_reader_file_t> &a) { return !a->input_reader->done() || a->input_split != a->sort_done;});
  for (auto it = finished; it != _point_reader_files.end(); ++it)
  {
    auto &input_reader = it->get()->input_reader;
    assert(input_reader->done());
    if (input_reader->error)
    {
      file_error_t file_error;
      file_error.error = std::move(*input_reader->error);
      file_error.input_id = input_reader->file.id;
      _file_errors.post_event(std::move(file_error));
    }
    _done_with_file.post_event(it->get()->input_reader->storage_header.input_id);
  }
  _point_reader_files.erase(finished, _point_reader_files.end());
}

void point_reader_t::handle_new_files(get_points_file_t &&new_file)
{
  _point_reader_files.emplace_back(new point_reader_file_t(_tree_state, _event_loop, _attributes_configs, new_file, _input_init_pipe, _sub_added, _unsorted_points, _sorted_points_pipe));
}

void point_reader_t::handle_unsorted_points(unsorted_points_event_t &&unsorted_points)
{
  auto &tree_state = unsorted_points.reader_file.tree_state;
  auto &reader_file = unsorted_points.reader_file;
  unsorted_points.reader_file.sort_workers.emplace_back(new sort_worker_t(tree_state, reader_file, _attributes_configs, unsorted_points.public_header, std::move(unsorted_points.points)));
  unsorted_points.reader_file.sort_workers.back()->enqueue(unsorted_points.reader_file.event_loop);
}

  
}
} // namespace points
