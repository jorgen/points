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

#include <uv.h>
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <mutex>

#include <points/converter/converter.h>

#include "conversion_types_p.h"
#include "event_pipe_p.h"
#include "reader_p.h"
#include "threaded_event_loop_p.h"

#include "tree_p.h"

namespace points
{
namespace converter
{
class processor_t
{
public:
  processor_t(converter_t &converter);
  void add_files(const std::vector<std::string> &files);
  //void add_data(const void *data, size_t data_size);

private:
  converter_t &converter;
  threaded_event_loop_t event_loop;
  event_pipe_t<points_t> sorted_points;
  event_pipe_t<file_error_t> file_errors;
  point_reader_t sorter;

  tree_global_state_t tree_global_state;
  tree_t tree;
  bool tree_initialized;

  void handle_sorted_points(std::vector<points_t> &&sorted_points);
  void handle_file_errors(std::vector<file_error_t> &&errors);
};
}
} // namespace points
