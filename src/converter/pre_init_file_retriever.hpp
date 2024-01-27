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
#include "input_data_source_registry.hpp"

namespace points
{
namespace converter
{
struct get_file_pre_init_t
{
  input_data_id_t id;
  input_name_ref_t filename;
};

struct pre_init_info_file_result_t
{
  input_data_id_t id;
  double min[3];
  uint64_t approximate_point_count;
  uint8_t approximate_point_size_bytes;
  bool found_min;
};

struct get_pre_init_info_worker_t : worker_t
{
  get_pre_init_info_worker_t(const tree_global_state_t &tree_state
                            , input_data_id_t input_id
                            , const input_name_ref_t &file_name
                            , converter_file_convert_callbacks_t &convert_callbacks
                            , event_pipe_single_t<pre_init_info_file_result_t> &pre_init_for_file
                            , event_pipe_single_t<file_error_t> &file_errors);
  void work() override;
  void after_work(completion_t completion) override;

  const tree_global_state_t &tree_state;
  input_data_id_t input_id;
  input_name_ref_t file_name;
  converter_file_convert_callbacks_t &converter_callbacks;
  event_pipe_single_t<pre_init_info_file_result_t> &pre_init_info_file_result;
  event_pipe_single_t<file_error_t> &file_errors;
};
struct get_pre_init_file_batch_t
{
  get_pre_init_file_batch_t(event_pipe_single_t<pre_init_info_file_result_t> &pre_init_info_result_pipe, event_pipe_single_t<file_error_t> &file_errors)
    : convert_callbacks(convert_callbacks)
    , pre_init_info_result_pipe(pre_init_info_result_pipe)
    , file_errors(file_errors)
    , input_done(0)
  {
  }
  std::vector<get_file_pre_init_t> input;
  converter_file_convert_callbacks_t &convert_callbacks;
  event_pipe_single_t<pre_init_info_file_result_t> &pre_init_info_result_pipe;
  event_pipe_single_t<file_error_t> &file_errors;
  std::vector<std::unique_ptr<worker_t>> workers;
  uint32_t input_done;
};

} // namespace converter
}

