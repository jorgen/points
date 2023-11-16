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
#include "attributes_configs.hpp"
#include "deque_map.hpp"

#include <memory>
#include <unordered_map>
#include <deque>

namespace points
{
namespace converter
{

struct write_id_t
{
  uint64_t data;
};

struct request_id_t
{
  uint64_t data;
};

struct cache_entry_t
{
  uint64_t offset;
  uint32_t size;
};

class cache_file_handler_t;

struct cache_file_request_t
{
  cache_file_request_t(cache_file_handler_t &cache_file_handler);
    
  void do_read(request_id_t id, const std::weak_ptr<cache_file_request_t> &self, int64_t offset, int32_t size);
  void do_write(request_id_t id, const std::weak_ptr<cache_file_request_t> &self, const std::shared_ptr<uint8_t[]> &data, int64_t offset, int32_t size);
  
  cache_file_handler_t &cache_file_handler;
 
  request_id_t id;
  std::shared_ptr<uint8_t[]> buffer;
  error_t error;

  uv_buf_t uv_buffer;
  uv_fs_t uv_request;
};

struct points_cache_item_t
{
  storage_header_t header;
  buffer_t data;
};

class cache_file_handler_t
{
public:
  cache_file_handler_t(const tree_global_state_t &state, const std::string &cache_file, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &cache_file_error, event_pipe_t<storage_header_t> &write_done);

  void handle_open_cache_file(uv_fs_t *request);

  std::vector<request_id_t> write(const storage_header_t &header, attribute_buffers_t &&buffers, std::function<void(request_id_t id, const error_t &error)> on_done);
  request_id_t read(input_data_id_t id, int attribute_index);

  bool attribute_id_and_count_for_input_id(input_data_id_t input_id, attributes_id_t &attributes_id, point_count_t &count);

  points_cache_item_t ref_points(input_data_id_t id, int attribute_index);
  void deref_points(input_data_id_t id);

  int fill_ids(uint32_t **ids, uint32_t **subs, int buffer_size);
  int item_count();

  bool is_available(input_data_id_t id, int attribute_index);

private:
  void handle_write_events(std::vector<std::tuple<std::vector<request_id_t>, storage_header_t, attribute_buffers_t, std::function<void(request_id_t id, const error_t &error)>>> &&events);
  void handle_request_done(request_id_t id);
  std::string _cache_file_name;
  threaded_event_loop_t _event_loop;

  const tree_global_state_t &_state;
  attributes_configs_t &_attributes_configs;
  uv_file _file_handle;
  bool _file_opened;

  uv_fs_t _open_request;
  event_pipe_t<error_t> &_cache_file_error;
  event_pipe_t<storage_header_t> &_write_done;
  event_pipe_t<std::tuple<std::vector<request_id_t>, storage_header_t, attribute_buffers_t, std::function<void(request_id_t id, const error_t &error)>>> _write_event_pipe;


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
    storage_header_t header;
    attributes_id_t attribute_id;
    attribute_buffers_t buffers;
  };

  std::mutex _mutex;

  deque_map_t<request_id_t, std::shared_ptr<cache_file_request_t>> _requests;

  std::unordered_map<input_data_id_t, cache_item_impl_t, hash_input_data_id_t> _cache_map;

  friend struct cache_file_request_t;
  friend static void request_done_callback(uv_fs_t *req);
};

struct read_points_t
{
  read_points_t(cache_file_handler_t &cache_file_handler, input_data_id_t id, int attribute_index)
    : cache_file_handler(cache_file_handler)
    , id(id)
    , cache_item(cache_file_handler.ref_points(id, attribute_index))
    , header(cache_item.header)
    , data(cache_item.data)
  {}
  ~read_points_t()
  {
    cache_file_handler.deref_points(id);
  }

  cache_file_handler_t &cache_file_handler;
  input_data_id_t id;
  points_cache_item_t cache_item;
  storage_header_t &header;
  buffer_t &data;
};
}
} // namespace points
