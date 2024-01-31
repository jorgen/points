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
#include "pre_init_file_retriever.hpp"

namespace points
{
namespace converter
{

get_pre_init_info_worker_t::get_pre_init_info_worker_t(const tree_global_state_t &tree_state, input_data_id_t input_id, const input_name_ref_t &file_name, converter_file_convert_callbacks_t &convert_callbacks,
                                                       event_pipe_t<pre_init_info_file_result_t> &pre_init_info_file_result, event_pipe_t<file_error_t> &file_errors)
  : tree_state(tree_state)
  , input_id(input_id)
  , file_name(file_name)
  , converter_callbacks(convert_callbacks)
  , pre_init_info_file_result(pre_init_info_file_result)
  , file_errors(file_errors)
{
}

void get_pre_init_info_worker_t::work()
{
  error_t *local_error = nullptr;
  auto pre_init_info = converter_callbacks.pre_init(file_name.name, file_name.name_length, &local_error);
  if (local_error)
  {
    std::unique_ptr<error_t> error(local_error);
    _file_error.input_id = input_id;
    _file_error.error = std::move(*error);
  }
  else
  {
    _file_error.error.code = 0;
    _pre_init_file.id = input_id;
    _pre_init_file.found_min = pre_init_info.found_aabb_min;
    memcpy(_pre_init_file.min, pre_init_info.aabb_min, sizeof(_pre_init_file.min));
    _pre_init_file.approximate_point_count = pre_init_info.approximate_point_count;
    _pre_init_file.approximate_point_size_bytes = pre_init_info.approximate_point_size_bytes;
  }
}

void get_pre_init_info_worker_t::after_work(completion_t completion)
{
  (void)completion;
  if (_file_error.error.code != 0)
    file_errors.post_event(std::move(_file_error));
  else
    pre_init_info_file_result.post_event(std::move(_pre_init_file));

}

}
} // namespace points
