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

#include "attributes_configs.hpp"
#include "blob_manager.hpp"
#include "conversion_types.hpp"
#include "error.hpp"
#include "event_pipe.hpp"
#include "threaded_event_loop.hpp"
#include "tree.hpp"
#include "worker.hpp"
#include <ankerl/unordered_dense.h>

#include <memory>

#include <cstdint>
#include <deque>
namespace points
{
namespace converter
{

struct write_tree_registry_request_t
{
  serialized_tree_registry_t serialized_tree_registry;
  std::function<void(storage_location_t, error_t &&error)> done_callback;
  storage_location_t location;
  error_t error;
};

struct write_trees_request_t
{
  int target_count = 0;
  int done = 0;
  std::vector<serialized_tree_t> serialized_trees;
  std::vector<tree_id_t> tree_ids;
  std::vector<storage_location_t> locations;
  error_t error;
  std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)> done_callback;
};

struct write_requests_t
{
  int target_count = 0;
  int done = 0;
  storage_header_t header;
  attributes_id_t attributes_id;
  std::vector<storage_location_t> locations;
  error_t error;
  std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)> done_callback;
};

class storage_handler_t;

struct cache_file_request_t
{
  cache_file_request_t(storage_handler_t &cache_file_handler, std::function<void(const cache_file_request_t &)> done_callback);

  void do_read(uint64_t offset, uint32_t size);
  void do_write(const std::shared_ptr<uint8_t[]> &data, uint64_t offset, uint32_t size);

  storage_handler_t &cache_file_handler;
  std::function<void(const cache_file_request_t &)> done_callback;

  std::shared_ptr<uint8_t[]> buffer;
  buffer_t buffer_info;
  error_t error;

  uv_buf_t uv_buffer;
  uv_fs_t uv_request;
};

struct read_request_t : cache_file_request_t
{
  read_request_t(storage_handler_t &cache_file_handler, std::function<void(const cache_file_request_t &)> done_callback);

  void wait_for_read();

  bool _done;
  std::mutex _mutex;
  std::condition_variable _block_for_read;
};

class storage_handler_t
{
public:
  storage_handler_t(const tree_global_state_t &state, const std::string &url, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &storage_error_pipe);

  void handle_open_cache_file(uv_fs_t *request);
  error_t wait_for_open();

  void write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
             std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)> done);
  void write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)> done);
  void write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done);
  void write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> done);

  std::shared_ptr<read_request_t> read(storage_location_t location, std::function<void(const cache_file_request_t &)> done_callback);

  void add_request(std::shared_ptr<cache_file_request_t> request);
  void remove_request(cache_file_request_t *request);

private:
  void handle_write_events(
    std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event);
  void handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)>> &&event);
  void handle_write_tree_registry(serialized_tree_registry_t &&serialized_trr, std::function<void(storage_location_t, error_t &&error)> &&done);
  void handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> &&done);
  void handle_write_index(free_blob_manager_t &&new_blob_manager, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                          std::function<void(error_t &&error)> &&done);
  void handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location);
  void remove_write_requests(write_requests_t *write_requests);
  void remove_write_tree_requests(write_trees_request_t *write_requests);
  void remove_write_tree_registry_requests(write_tree_registry_request_t *write_requests);

  std::string _cache_file_name;
  threaded_event_loop_t _event_loop;
  uv_file _file_handle;
  std::atomic_bool _file_opened;
  std::condition_variable _block_for_open;
  error_t _open_error;

  const tree_global_state_t &_state;
  attributes_configs_t &_attributes_configs;
  uint32_t _serialized_index_size;
  free_blob_manager_t _blob_manager;

  storage_location_t attributes_location;
  storage_location_t blobs_location;

  uv_fs_t _open_request{};
  event_pipe_t<error_t> &_storage_error;
  event_pipe_t<std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)>>> _write_event_pipe;
  event_pipe_t<std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)>>> _write_trees_pipe;
  event_pipe_t<serialized_tree_registry_t, std::function<void(storage_location_t, error_t &&error)>> _write_tree_registry_pipe;
  event_pipe_t<storage_location_t, std::vector<storage_location_t>, std::function<void(error_t &&error)>> _write_blob_locations_and_update_header_pipe;
  event_pipe_t<std::shared_ptr<read_request_t>, storage_location_t> _read_request_pipe;

  std::mutex _mutex;

  std::vector<std::unique_ptr<write_requests_t>> _write_requests;
  std::vector<std::unique_ptr<write_trees_request_t>> _write_trees_requests;
  std::vector<std::unique_ptr<write_tree_registry_request_t>> _write_tree_registry_requests;

  std::mutex _requests_mutex;
  std::deque<std::shared_ptr<cache_file_request_t>> _requests;

  friend struct cache_file_request_t;
};

static bool deserialize_points(const buffer_t &data, storage_header_t &header, buffer_t &point_data, error_t &error)
{
  if (data.size < sizeof(header))
  {
    error.code = 2;
    error.msg = "Invalid input size";
    return false;
  }
  auto input_bytes = static_cast<uint8_t *>(data.data);
  memcpy(&header, input_bytes, sizeof(header));
  point_data.size = data.size - sizeof(header);
  point_data.data = input_bytes + sizeof(header);
  return true;
}

struct read_only_points_t
{
  read_only_points_t(storage_handler_t &cache_file_handler, storage_location_t location)
    : cache_file_handler(cache_file_handler)
    , location(location)
    , read_request(cache_file_handler.read(location, [this](const cache_file_request_t &) {}))
  {
    read_request->wait_for_read();
    deserialize_points(read_request->buffer_info, header, data, error);
  }
  ~read_only_points_t()
  {
  }

  storage_handler_t &cache_file_handler;
  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  storage_header_t header;
  buffer_t data;
  error_t error;
};

struct read_attribute_t
{
  read_attribute_t(storage_handler_t &cache_file_handler, storage_location_t location)
    : cache_file_handler(cache_file_handler)
    , location(location)
    , read_request(cache_file_handler.read(location, [this](const cache_file_request_t &) {}))
  {
    read_request->wait_for_read();
    data = read_request->buffer_info;
  }

  storage_handler_t &cache_file_handler;
  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  buffer_t data;
  error_t error;
};

} // namespace converter
} // namespace points
