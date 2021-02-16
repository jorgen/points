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

sorter_t::sorter_t(event_pipe_t<points_t> &sorted_points_pipe, event_pipe_t<error_t> &file_errors)
  : sorted_points_pipe(sorted_points_pipe)
  , file_errors(file_errors)
  , new_files_pipe(event_loop, [this](std::vector<std::vector<std::string>> &&new_files) { handle_new_files(std::move(new_files));})
{
}

void sorter_t::add_files(const std::vector<std::string> &files)
{
  new_files_pipe.post_event(files);
}

void sorter_t::handle_new_files(std::vector<std::vector<std::string>> &&new_files)
{
  std::vector<std::string> collapsed_new_files;
  for (auto &files : new_files)
  {
    collapsed_new_files.insert(collapsed_new_files.end(), files.begin(), files.end());
  }

  fmt::print(stderr, "handle_new_files size: {} first {}\n", collapsed_new_files.size(), collapsed_new_files[0]);
}

  
}
} // namespace points
