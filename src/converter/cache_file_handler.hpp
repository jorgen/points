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

namespace points
{
namespace converter
{

struct cache_file_t
{
  cache_file_t(threaded_event_loop_t &event_loop, const std::string &file_name)
    : _file_name(file_name)
    , _uv_file(0)
    , _event_loop(event_loop)
  {
  }
  std::string _file_name;
  uv_file _uv_file;
  threaded_event_loop_t &_event_loop;
};

struct cache_file_read_request_t
{
  cache_file_read_request_t(cache_file_t &cache_file, int64_t offset, int32_t size, std::function<void(std::unique_ptr<uint8_t[]> &&, error_t &&)> read_function)
    : buffer(new uint8_t[size])
  {
    read_request.data = this;
    uv_buffer.base = (char*) buffer.get();
    uv_buffer.len = size;
    uv_fs_read(cache_file._event_loop.loop(), &read_request, cache_file._uv_file, &uv_buffer, 1, offset, [](uv_fs_t *req) {
      error_t error;
      if (req->result < 0)
      {
        error.code = req->result;
        error.msg = uv_strerror(req->result);
      }
      uv_fs_req_cleanup(req);
      auto *read_request = static_cast<cache_file_read_request_t *>(req->data);
      read_request->callback(std::move(read_request->buffer), std::move(error));
      read_request->read_request.data = nullptr;
    });
  }
  ~cache_file_read_request_t()
  {
  }

  std::unique_ptr<uint8_t[]> buffer;
  uv_buf_t uv_buffer;
  uv_fs_t read_request;
  std::function<void(std::unique_ptr<uint8_t[]> &&, error_t &&)> callback;
};

class cache_file_handler_t : public about_to_block_t
{
public:
  cache_file_handler_t(const std::string &cache_file, event_pipe_t<error_t> &cache_file_error);

  void about_to_block();

  void handle_open_cache_file(uv_fs_t *request);
private:
  std::string _cache_file;
  threaded_event_loop_t _event_loop;

  event_pipe_t<error_t> &_cache_file_error;

};
}
} // namespace points
