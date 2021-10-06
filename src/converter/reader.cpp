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
#include "reader_p.h"

#include "threaded_event_loop_p.h"
#include "morton_p.h"
#include "sorter_p.h"
#include "morton_tree_coordinate_transform_p.h"

#include <fmt/printf.h>

#include <points/converter/default_attribute_names.h>

#include <assert.h>

namespace points
{
namespace converter
{
get_header_worker_t::get_header_worker_t(batch_get_headers_t &batch_get_headers, const std::string file_name, bool close)
  : batch(batch_get_headers)
  , file_name(file_name)
  , close(close)
  , user_ptr(nullptr)
{
}
void get_header_worker_t::work()
{
  header.name = file_name;
  error_t *local_error = nullptr;
  batch.convert_callbacks.init(file_name.c_str(), file_name.size(), &header, &user_ptr, &local_error);
  if (local_error)
    error.reset(local_error);
  if (close && batch.convert_callbacks.destroy_user_ptr)
  {
    batch.convert_callbacks.destroy_user_ptr(user_ptr);
    user_ptr = nullptr;
  }
}

void get_header_worker_t::after_work(completion_t completion)
{
  (void)completion;
  batch.completed++;
}

get_data_worker_t::get_data_worker_t(converter_file_convert_callbacks_t &convert_callbacks, input_file_t &input_file, std::vector<get_data_worker_t *> &done_list)
  : convert_callbacks(convert_callbacks)
  , input_file(input_file)
  , points_read(0)
  , done_list(done_list)
{
}

void get_data_worker_t::work()
{
  int convert_size = 20000;
  attribute_buffers_initialize(input_file.header.attributes.attributes, buffers, convert_size);
  error_t *local_error = nullptr;
  points_read = convert_callbacks.convert_data(input_file.user_ptr, &input_file.header, input_file.header.attributes.attributes.data(), input_file.header.attributes.attributes.size(), buffers.buffers.data(), buffers.buffers.size(), convert_size, &input_file.done, &local_error);
  if (local_error)
    error.reset(local_error);
  attribute_buffers_adjust_buffers_to_size(input_file.header.attributes.attributes, buffers, points_read);
  if ((input_file.user_ptr && (input_file.done || (error && error->code)) && convert_callbacks.destroy_user_ptr))
  {
    convert_callbacks.destroy_user_ptr(input_file.user_ptr); 
    input_file.user_ptr = nullptr;
  }
}

void get_data_worker_t::after_work(completion_t completion)
{
  (void)completion;
  done_list.push_back(this);
}
  
sort_worker_t::sort_worker_t(const tree_global_state_t &tree_state, const internal_header_t &header, attribute_buffers_t &&buffers, uint64_t point_count, std::vector<sort_worker_t *> &done_list)
  : tree_state(tree_state)
  , done_list(done_list)
{
  header_copy(header, points.header);
  points.header.point_count = point_count;
  points.buffers = std::move(buffers);
}

void sort_worker_t::work()
{
  sort_points(tree_state, points); 
}

void sort_worker_t::after_work(completion_t completion)
{
  (void)completion;
  done_list.push_back(this);
}

point_reader_t::point_reader_t(const tree_global_state_t &tree_state, event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<file_error_t> &file_errors, converter_file_convert_callbacks_t &convert_callbacks)
  : tree_state(tree_state)
  , sorted_points_pipe(sorted_points_pipe)
  , file_errors(file_errors)
  , new_files_pipe(event_loop, [this](std::vector<std::vector<std::string>> &&new_files) { handle_new_files(std::move(new_files));})
  , convert_callbacks(convert_callbacks)
  , active_converters(0)
  , max_converters(4)
{
  input_files.reserve(100);
  event_loop.add_about_to_block_listener(this);
}

void point_reader_t::add_files(const std::vector<std::string> &files)
{
  new_files_pipe.post_event(files);
}

static int get_next_nonactive_input_file(const std::vector<std::unique_ptr<input_file_t>> &input_files)
{
  int i;
  for (i = 0; i < int(input_files.size()); i++)
  {
    if (!input_files[i]->active_worker)
      return i;
  }
  return i;
}

void point_reader_t::about_to_block()
{
  uint32_t files_inserted = 0;
  auto to_remove_begin =
    std::remove_if(get_headers_batch_jobs.begin(), get_headers_batch_jobs.end(), [](const std::unique_ptr<batch_get_headers_t> &batch) { return batch->completed == uint32_t(batch->get_headers.size()); });
  bool needs_sort = false;
  if (to_remove_begin != get_headers_batch_jobs.end())
  {
    for (auto it = to_remove_begin; it != get_headers_batch_jobs.end(); ++it)
    {
      auto &batch = *it;
      if (batch->completed == batch->get_headers.size())
      {
        files_inserted += uint32_t(batch->get_headers.size());
        for (auto &header : batch->get_headers)
        {
          if (header.error && header.error->code)
          {
            file_error_t file_error;
            file_error.filename = header.header.name;
            file_error.error = *header.error;
            file_errors.post_event(std::move(file_error));
          } else
          {
            convert_pos_to_morton(header.header.scale, header.header.offset, header.header.min, header.header.morton_min);
            convert_pos_to_morton(header.header.scale, header.header.offset, header.header.max, header.header.morton_max);
            input_files.emplace_back(new input_file_t());
            input_files.back()->header = std::move(header.header);
            input_files.back()->user_ptr = header.user_ptr;
            input_files.back()->done = false;
          }
        }
      }
    }
    get_headers_batch_jobs.erase(to_remove_begin, get_headers_batch_jobs.end());
    needs_sort = true;
  }
   
  if (needs_sort)
    std::sort(input_files.begin(), input_files.end(), [](const std::unique_ptr<input_file_t> &a, const std::unique_ptr<input_file_t> &b) { return morton::morton_lt(a->header.morton_max, b->header.morton_max); });

  std::vector<input_file_t *> done_input_files;
  for (auto &finished : finished_get_workers)
  {
    if (finished->error && finished->error->code)
    {
      file_error_t error;
      error.filename = finished->input_file.header.name;
      error.error = std::move(*finished->error);
      file_errors.post_event(std::move(error));
      done_input_files.push_back(&finished->input_file);
      finished->input_file.active_worker.reset();
    }
    else
    {
      sort_workers.emplace_back(new sort_worker_t(tree_state, finished->input_file.header, std::move(finished->buffers), finished->points_read, finished_sort_workers));
      sort_workers.back()->enqueue(event_loop);
      if (finished->input_file.done)
        done_input_files.push_back(&finished->input_file);
      finished->input_file.active_worker.reset();
    }
  }

  active_converters -= uint32_t(finished_get_workers.size());
  finished_get_workers.clear();

  for (auto done_input_file : done_input_files)
  {
    auto it = std::find_if(input_files.begin(), input_files.end(), [done_input_file](const std::unique_ptr<input_file_t> &a) { return a.get() == done_input_file; });
    //assert(it != input_files.end());
    fmt::print(stderr, "Done processing inputfile {}\n", it->get()->header.name);
    input_files.erase(it); 
  }

  for (uint32_t i = active_converters; i < max_converters; i++)
  {
    auto nonactive_index = get_next_nonactive_input_file(input_files);
    if (nonactive_index == int(input_files.size()))
      break;
    auto &next = input_files[nonactive_index];
    next->active_worker.reset(new get_data_worker_t(convert_callbacks, *next, finished_get_workers));
    next->active_worker->enqueue(event_loop);
    active_converters++;
  }

  for (auto &finished_sort : finished_sort_workers)
  {
    sorted_points_pipe.post_event(std::move(finished_sort->points));
  }
  finished_sort_workers.clear();
}

void point_reader_t::handle_new_files(std::vector<std::vector<std::string>> &&new_files)
{
  if (convert_callbacks.init == nullptr || convert_callbacks.convert_data == nullptr)
  {
    file_error_t error;
    error.error = {-1, std::string("Conversion callbacks has nullptr for init or convert_data functions. Its therefor not possible to convert the data")};
    file_errors.post_event(std::move(error));
    return;
  }
  std::vector<std::string> collapsed_new_files;
  for (auto &files : new_files)
  {
    collapsed_new_files.insert(collapsed_new_files.end(), files.begin(), files.end());
  }
  get_headers_batch_jobs.emplace_back(new batch_get_headers_t());
  auto &batch_headers = get_headers_batch_jobs.back();
  batch_headers->convert_callbacks = convert_callbacks;
  batch_headers->get_headers.reserve(collapsed_new_files.size());
  for (auto &file : collapsed_new_files)
  {
    auto inserted = all_input_filenames.insert(file);
    if (inserted.second)
    {
      batch_headers->get_headers.emplace_back(*batch_headers.get(), file, collapsed_new_files.size() > 100);
      batch_headers->get_headers.back().enqueue(event_loop);
    }
  }
  if (batch_headers->get_headers.empty())
    get_headers_batch_jobs.pop_back();

  fmt::print(stderr, "handle_new_files size: {} first {}\n", collapsed_new_files.size(), collapsed_new_files[0]);
}

  
}
} // namespace points
