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
struct get_file_pre_init_t
{
  input_data_id_t id;
  input_name_ref_t filename;
};

inline get_file_pre_init_t get_file_pre_init_from_input_data_source(const input_data_source_t &a)
{
  get_file_pre_init_t ret;
  ret.id = a.input_id;
  ret.filename = input_name_ref_from_input_data_source(a);
  return ret;
}

struct pre_init_info_file_t
{
  input_data_id_t id;
  double min[3];
  uint64_t approximate_point_count;
  uint8_t approximate_point_size_bytes;
  bool found_min;
};

struct get_pre_init_file_batch_t
{
  get_pre_init_file_batch_t(converter_file_convert_callbacks_t &convert_callbacks, event_pipe_t<pre_init_info_file_t> &aabb_min_event_pipe, event_pipe_t<file_error_t> &file_errors)
    : convert_callbacks(convert_callbacks)
    , pre_init_file_event_pipe(aabb_min_event_pipe)
    , file_errors(file_errors)
    , input_done(0)
  {
  }
  std::vector<get_file_pre_init_t> input;
  converter_file_convert_callbacks_t &convert_callbacks;
  event_pipe_t<pre_init_info_file_t> &pre_init_file_event_pipe;
  event_pipe_t<file_error_t> &file_errors;
  std::vector<std::unique_ptr<worker_t>> workers;
  uint32_t input_done;
};

class pre_init_file_retriever_t : public about_to_block_t
{
public:
  pre_init_file_retriever_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, event_pipe_t<pre_init_info_file_t> &pre_init_for_file, event_pipe_t<file_error_t> &file_errors);
  void add_files(std::vector<get_file_pre_init_t> files, converter_file_convert_callbacks_t &convert_callbacks);
  void about_to_block() override;

private:
  void handle_new_files(std::vector<get_pre_init_file_batch_t> &&input_files_batch);
  const tree_global_state_t &_tree_state;
  threaded_event_loop_t &_event_loop;

  std::vector<std::unique_ptr<get_pre_init_file_batch_t>> _input_batches;
  event_pipe_t<get_pre_init_file_batch_t> _add_files_pipe;

  event_pipe_t<pre_init_info_file_t> &_pre_init_for_file;
  event_pipe_t<file_error_t> &_file_errors;

};

} // namespace converter
}

