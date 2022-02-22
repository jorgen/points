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

#include <memory>
#include <unordered_map>

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

struct points_cache_item_t
{
  internal_header_t header;
  format_t format;
  components_t components;
  buffer_t data;
};

class cache_file_handler_t : public about_to_block_t
{
public:
  cache_file_handler_t(const tree_global_state_t &state, const std::string &cache_file, event_pipe_t<error_t> &cache_file_error, event_pipe_t<internal_header_t> &write_done);

  void about_to_block();

  void handle_open_cache_file(uv_fs_t *request);

  void write(const internal_header_t &header, attribute_buffers_t &&buffers, attributes_t *attributes);

  points_cache_item_t ref_points(input_data_id_t id);
  void deref_points(input_data_id_t id);

private:
  void handle_write_events(std::vector<std::tuple<internal_header_t, attribute_buffers_t, attributes_t *>> &&events);
  std::string _cache_file_name;
  threaded_event_loop_t _event_loop;

  const tree_global_state_t &_state;
  uv_file _file_handle;
  bool _file_opened;

  uv_fs_t _open_request;
  event_pipe_t<error_t> &_cache_file_error;
  event_pipe_t<internal_header_t> &_write_done;
  event_pipe_t<std::tuple<internal_header_t, attribute_buffers_t, attributes_t *>> _write_event_pipe;

  struct hash_input_data_id_t
  {
    size_t operator()(const input_data_id_t &a) const
    {
      uint64_t b;
      static_assert(sizeof(a) == sizeof(b), "hash function is invalid");
      memcpy(&b, &a, sizeof(b));
      return std::hash<decltype (b)>()(b);
    }
  };
  struct cache_item_impl_t
  {
    int ref;
    internal_header_t header;
    attribute_buffers_t buffers;
    format_t format;
    components_t components;
  };

  std::mutex _cache_map_mutex;
  std::unordered_map<input_data_id_t, cache_item_impl_t, hash_input_data_id_t> _cache_map;
};

struct read_points_t
{
  read_points_t(cache_file_handler_t &cache_file_handler, input_data_id_t id)
    : cache_file_handler(cache_file_handler)
    , id(id)
    , cache_item(cache_file_handler.ref_points(id))
    , header(cache_item.header)
    , data(cache_item.data)
    , format(cache_item.format)
    , components(cache_item.components)
  {}
  ~read_points_t()
  {
    cache_file_handler.deref_points(id);
  }
  cache_file_handler_t &cache_file_handler;
  input_data_id_t id;
  points_cache_item_t cache_item;
  internal_header_t &header;
  buffer_t &data;
  format_t &format;
  components_t &components;
};
}
} // namespace points
