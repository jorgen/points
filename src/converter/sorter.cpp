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
#include "sorter_p.h"

#include "threaded_event_loop_p.h"

#include <fmt/printf.h>

namespace points
{
namespace converter
{
//void sorter_t::on_new_input_files(uv_async_t *handle)
//{
//  sorter_t *sorter = static_cast<sorter_t*>(handle->data);
//  std::vector<std::string> process_files;
//  {
//    std::unique_lock<std::mutex> lock(sorter->event_loop->mutex);
//    process_files = std::move(sorter->input_files);
//  }
//
//
//}

get_header_worker_t::get_header_worker_t(batch_get_headers_t &batch_get_headers, const std::string file_name, bool close)
  : batch(batch_get_headers)
  , file_name(file_name)
  , close(close)
  , error(nullptr)
{
}
void get_header_worker_t::work()
{
  batch.convert_callbacks.init(file_name.c_str(), file_name.size(), &header, &header.user_ptr, &error);
  if (close && batch.convert_callbacks.destroy_user_ptr)
    batch.convert_callbacks.destroy_user_ptr(header.user_ptr);
}

void get_header_worker_t::after_work(completion_t completion)
{
  (void)completion;
  batch.completed++;
}

sorter_t::sorter_t(event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<error_t> &file_errors, converter_file_convert_callbacks_t &convert_callbacks)
  : sorted_points_pipe(sorted_points_pipe)
  , file_errors(file_errors)
  , new_files_pipe(event_loop, [this](std::vector<std::vector<std::string>> &&new_files) { handle_new_files(std::move(new_files));})
  , convert_callbacks(convert_callbacks)
{
  event_loop.add_about_to_block_listener(this);
}

void sorter_t::add_files(const std::vector<std::string> &files)
{
  new_files_pipe.post_event(files);
}

void sorter_t::about_to_block()
{
  fmt::print(stderr, "about to block {}\n", (void*) this);
  for (auto &batch : get_headers)
  {
    if (batch->completed == batch->get_headers.size())
    {
      for (auto &header : batch->get_headers)
      {
        
      }
    }
  }
}

void sorter_t::handle_new_files(std::vector<std::vector<std::string>> &&new_files)
{
  if (convert_callbacks.init == nullptr || convert_callbacks.convert_data == nullptr)
  {
    file_errors.post_event({-1, std::string("Conversion callbacks has nullptr for init or convert_data functions. Its therefor not possible to convert the data")});
    return;
  }
  std::vector<std::string> collapsed_new_files;
  for (auto &files : new_files)
  {
    collapsed_new_files.insert(collapsed_new_files.end(), files.begin(), files.end());
  }
  get_headers.emplace_back(new batch_get_headers_t());
  auto &batch_headers = get_headers.back();
  batch_headers->convert_callbacks = convert_callbacks;
  batch_headers->get_headers.reserve(collapsed_new_files.size());
  for (auto &file : collapsed_new_files)
  {
    batch_headers->get_headers.emplace_back(*batch_headers.get(), file, collapsed_new_files.size() > 100);
    batch_headers->get_headers.back().enqueue(event_loop);
  }

  fmt::print(stderr, "handle_new_files size: {} first {}\n", collapsed_new_files.size(), collapsed_new_files[0]);
}

  
}
} // namespace points
