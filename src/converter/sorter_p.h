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

#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <points/converter/converter.h>
#include <functional>

#include "conversion_types_p.h"
#include "error_p.h"
#include "event_pipe_p.h"
#include "threaded_event_loop_p.h"

namespace points
{
namespace converter
{
struct input_files
{
  std::string name;
  uint64_t size;
  
};

class sorter_t
{
public:
  sorter_t(event_pipe_t<points_t>& sorted_points_pipe, event_pipe_t<error_t> &file_errors);
  void add_files(const std::vector<std::string> &files);
  //void add_data(const void *data, size_t data_size);

private:
  void handle_new_files(std::vector<std::vector<std::string>> &&new_files);

  threaded_event_loop_t event_loop;
  event_pipe_t<points_t> &sorted_points_pipe;
  event_pipe_t<error_t> &file_errors;
  event_pipe_t<std::vector<std::string>> new_files_pipe;

};
}
} // namespace points
