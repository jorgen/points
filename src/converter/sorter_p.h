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
  header_t header;
  bool close;
  error_t *error;
};

class batch_get_headers_t
{
public:
  std::vector<get_header_worker_t> get_headers;
  uint32_t completed = 0;
  converter_file_convert_callbacks_t convert_callbacks;
};

class sorter_t : public about_to_block_t
{
public:
  sorter_t(event_pipe_t<points_t>& sorted_points_pipe, event_pipe_t<error_t> &file_errors, converter_file_convert_callbacks_t &convert_callbacks);
  void add_files(const std::vector<std::string> &files);
  //void add_data(const void *data, size_t data_size);

  void about_to_block() override;

private:
  void handle_new_files(std::vector<std::vector<std::string>> &&new_files);

  threaded_event_loop_t event_loop;
  event_pipe_t<points_t> &sorted_points_pipe;
  event_pipe_t<error_t> &file_errors;
  event_pipe_t<std::vector<std::string>> new_files_pipe;

  std::vector<input_files> input_files;
  std::vector<std::unique_ptr<batch_get_headers_t>> get_headers;
  converter_file_convert_callbacks_t &convert_callbacks;
};
}
} // namespace points
