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
#include "header_reader.hpp"
namespace points
{
namespace converter
{

struct get_header_worker_t : worker_t
{
  get_header_worker_t(input_data_id_t input_id, const input_name_ref_t &file_name, get_header_files_batch_t &batch)
    : input_id(input_id)
    , file_name(file_name)
    , batch(batch)
    , user_ptr(nullptr)
  {
  }
  void work() override
  {
    internal_header_initialize(header);
    header.input_id = input_id;
    error_t *local_error = nullptr;
    batch.convert_callbacks.init(file_name.name, file_name.name_length, &header, &user_ptr, &local_error);
    error.reset(local_error);
    if (batch.convert_callbacks.destroy_user_ptr && user_ptr)
    {
      batch.convert_callbacks.destroy_user_ptr(user_ptr);
      user_ptr = nullptr;
    }
  }

  void after_work(completion_t completion) override
  {
    batch.input_done++;
    if (completion == completed)
    {
      if (error)
      {
        file_error_t file_error;
        file_error.input_id = input_id;
        file_error.error = std::move(*error);
        batch.file_errors.post_event(std::move(file_error));
      }
      else 
      {
        batch.headers_for_files.post_event(std::move(header)); 
      }
    }
  }

  input_data_id_t input_id;
  input_name_ref_t file_name;
  get_header_files_batch_t &batch;
  void *user_ptr;
  internal_header_t header;
  std::unique_ptr<error_t> error;
};

header_retriever_t::header_retriever_t(threaded_event_loop_t &event_loop, event_pipe_t<internal_header_t> &headers_for_files, event_pipe_t<file_error_t> &file_errors)
  : _event_loop(event_loop)
  , _add_files_pipe(_event_loop, [this](std::vector<get_header_files_batch_t> &&input_files_batch) { handle_new_files(std::move(input_files_batch)); })
  , _headers_for_files(headers_for_files)
  , _file_errors(file_errors)
{

}

void header_retriever_t::add_files(std::vector<get_header_files_t> files, converter_file_convert_callbacks_t &convert_callbacks)
{
  get_header_files_batch_t batch(convert_callbacks, _headers_for_files, _file_errors);
  batch.input.insert(batch.input.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
  _add_files_pipe.post_event(std::move(batch));
}

void header_retriever_t::about_to_block()
{
  {
    auto it = std::partition(_input_batches.begin(), _input_batches.end(), [](const std::unique_ptr<get_header_files_batch_t> &a) { return !(a->input.size() < a->input_done); });
    _input_batches.erase(it, _input_batches.end());
  }

}

void header_retriever_t::handle_new_files(std::vector<get_header_files_batch_t> &&input_files_batches)
{
  for (auto &&batch : input_files_batches)
  {
    _input_batches.emplace_back(new get_header_files_batch_t(batch.convert_callbacks, _headers_for_files, _file_errors));
    auto &input_batch = _input_batches.back();
    input_batch->input = std::move(batch.input);
    input_batch->workers.reserve(input_batch->input.size());
    for (auto &input : input_batch->input)
    {
      input_batch->workers.emplace_back(new get_header_worker_t(input.id, input.filename, *(input_batch.get())));
      input_batch->workers.back()->enqueue(_event_loop);
    }
  }
}

}
} // namespace points
