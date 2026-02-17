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

#include <vio/event_pipe.h>
#include <vio/worker.h>

#include "conversion_types.hpp"
#include "error.hpp"
#include "input_header.hpp"

namespace points::converter
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

struct get_pre_init_info_worker_t : vio::worker_t
{
  get_pre_init_info_worker_t(const tree_config_t &tree_config, input_data_id_t input_id, const input_name_ref_t &file_name, converter_file_convert_callbacks_t &convert_callbacks,
                             vio::event_pipe_t<pre_init_info_file_result_t> &pre_init_for_file, vio::event_pipe_t<file_error_t> &file_errors);
  void work() override;
  void after_work(completion_t completion) override;

  tree_config_t tree_config;
  input_data_id_t input_id;
  input_name_ref_t file_name;
  converter_file_convert_callbacks_t &converter_callbacks;
  vio::event_pipe_t<pre_init_info_file_result_t> &pre_init_info_file_result;
  vio::event_pipe_t<file_error_t> &file_errors;

  std::unique_ptr<error_t> _error;
  file_error_t _file_error;
  pre_init_info_file_result_t _pre_init_file;
};

} // namespace points::converter
