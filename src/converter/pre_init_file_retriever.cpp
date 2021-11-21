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

struct get_pre_init_info_worker_t : worker_t
{
  get_pre_init_info_worker_t(const tree_global_state_t &tree_state, input_data_id_t input_id, const input_name_ref_t &file_name, get_pre_init_file_batch_t &batch)
    : tree_state(tree_state)
    , input_id(input_id)
    , file_name(file_name)
    , batch(batch)
  {
  }
  void work() override
  {
    error_t *local_error = nullptr;
    auto pre_init_info = batch.convert_callbacks.pre_init(file_name.name, file_name.name_length, &local_error);  
    if (local_error)
    {
      std::unique_ptr<error_t> error(local_error);
      file_error_t file_error;
      file_error.input_id = input_id;
      file_error.error = std::move(*error);
      batch.file_errors.post_event(std::move(file_error));
    }
    else
    {
      pre_init_info_file_t pre_init_file;
      pre_init_file.found_min = pre_init_info.found_aabb_min;
      memcpy(pre_init_file.min, pre_init_info.aabb_min, sizeof(pre_init_file.min));
      pre_init_file.approximate_point_count = pre_init_info.approximate_point_count;
      pre_init_file.approximate_point_size_bytes = pre_init_info.approximate_point_size_bytes;
      batch.pre_init_file_event_pipe.post_event(std::move(pre_init_file));
    }
  }

  void after_work(completion_t completion) override
  {
    (void)completion;
    batch.input_done++;
  }

  const tree_global_state_t &tree_state;
  input_data_id_t input_id;
  input_name_ref_t file_name;
  get_pre_init_file_batch_t &batch;

};

pre_init_file_retriever_t::pre_init_file_retriever_t(const tree_global_state_t &tree_state, threaded_event_loop_t &event_loop, event_pipe_t<pre_init_info_file_t> &pre_init_for_file, event_pipe_t<file_error_t> &file_errors)
  : _tree_state(tree_state)
  , _event_loop(event_loop)
  , _add_files_pipe(_event_loop, [this](std::vector<get_pre_init_file_batch_t> &&input_files_batch) { handle_new_files(std::move(input_files_batch)); })
  , _pre_init_for_file(pre_init_for_file)
  , _file_errors(file_errors)
{
  _event_loop.add_about_to_block_listener(this);
}

void pre_init_file_retriever_t::add_files(std::vector<get_file_pre_init_t> files, converter_file_convert_callbacks_t &convert_callbacks)
{
  get_pre_init_file_batch_t batch(convert_callbacks, _pre_init_for_file, _file_errors);
  batch.input.insert(batch.input.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
  _add_files_pipe.post_event(std::move(batch));
}

void pre_init_file_retriever_t::about_to_block()
{
  {
    auto it = std::partition(_input_batches.begin(), _input_batches.end(), [](const std::unique_ptr<get_pre_init_file_batch_t> &a) { return !(a->input.size() < a->input_done); });
    _input_batches.erase(it, _input_batches.end());
  }

}

void pre_init_file_retriever_t::handle_new_files(std::vector<get_pre_init_file_batch_t> &&input_files_batches)
{
  for (auto &&batch : input_files_batches)
  {
    _input_batches.emplace_back(new get_pre_init_file_batch_t(batch.convert_callbacks, _pre_init_for_file, _file_errors));
    auto &input_batch = _input_batches.back();
    input_batch->input = std::move(batch.input);
    input_batch->workers.reserve(input_batch->input.size());
    for (auto &input : input_batch->input)
    {
      input_batch->workers.emplace_back(new get_pre_init_info_worker_t(_tree_state, input.id, input.filename, *(input_batch.get())));
      input_batch->workers.back()->enqueue(_event_loop);
    }
  }
}

}
} // namespace points
