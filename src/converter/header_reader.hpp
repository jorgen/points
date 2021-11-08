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
struct get_header_files_t
{
  input_data_id_t id;
  input_name_ref_t filename;
};

inline get_header_files_t get_header_files_from_input_data_source(const input_data_source_t &a)
{
  get_header_files_t ret;
  ret.id = a.input_id;
  ret.filename = input_name_ref_from_input_data_source(a);
  return ret;
}

struct get_header_files_batch_t
{
  get_header_files_batch_t(converter_file_convert_callbacks_t &convert_callbacks, event_pipe_t<internal_header_t> &headers_for_files, event_pipe_t<file_error_t> &file_errors)
    : convert_callbacks(convert_callbacks)
    , headers_for_files(headers_for_files)
    , file_errors(file_errors)
    , input_done(0)
  {
  }
  std::vector<get_header_files_t> input;
  converter_file_convert_callbacks_t &convert_callbacks;
  event_pipe_t<internal_header_t> &headers_for_files;
  event_pipe_t<file_error_t> &file_errors;
  std::vector<std::unique_ptr<worker_t>> workers;
  uint32_t input_done;
};

class header_retriever_t : public about_to_block_t
{
public:
  header_retriever_t(threaded_event_loop_t &event_loop, event_pipe_t<internal_header_t> &headers_for_files, event_pipe_t<file_error_t> &file_errors);
  void add_files(std::vector<get_header_files_t> files, converter_file_convert_callbacks_t &convert_callbacks);
  void about_to_block() override;

private:
  void handle_new_files(std::vector<get_header_files_batch_t> &&input_files_batch);
  threaded_event_loop_t &_event_loop;

  std::vector<std::unique_ptr<get_header_files_batch_t>> _input_batches;
  event_pipe_t<get_header_files_batch_t> _add_files_pipe;

  event_pipe_t<internal_header_t> &_headers_for_files;
  event_pipe_t<file_error_t> &_file_errors;

};

} // namespace converter
}

