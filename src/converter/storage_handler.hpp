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

#include <stdint.h>
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

  void do_read(const std::weak_ptr<cache_file_request_t> &self, int64_t offset, int32_t size);
  void do_write(const std::shared_ptr<cache_file_request_t> &self, const std::shared_ptr<uint8_t[]> &data, uint64_t offset, uint32_t size);

  storage_handler_t &cache_file_handler;
  std::function<void(const cache_file_request_t &)> done_callback;

  std::shared_ptr<uint8_t[]> buffer;
  error_t error;

  uv_buf_t uv_buffer;
  uv_fs_t uv_request;
};

struct points_cache_item_t
{
  buffer_t data;
};

class storage_handler_t
{
public:
  storage_handler_t(const tree_global_state_t &state, std::string cache_file, attributes_configs_t &attributes_configs, event_pipe_t<error_t> &cache_file_error);

  void handle_open_cache_file(uv_fs_t *request);
  error_t wait_for_open();

  void write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
             std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)> done);
  void write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)> done);
  void write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done);
  void write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> done);
  void read(input_data_id_t id, int attribute_index);

  points_cache_item_t ref_points(const storage_location_t &location);
  void deref_points(const storage_location_t &location);

  int item_count();

  bool is_available(input_data_id_t id, int attribute_index);

  void remove_write_requests(write_requests_t *write_requests);
  void remove_write_tree_requests(write_trees_request_t *write_requests);
  void remove_write_tree_registry_requests(write_tree_registry_request_t *write_requests);

private:
  void handle_write_events(
    std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event);
  void handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)>> &&event);
  void handle_write_tree_registry(serialized_tree_registry_t &&serialized_trr, std::function<void(storage_location_t, error_t &&error)> &&done);
  void handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> &&done);
  void handle_write_index(free_blob_manager_t &&new_blob_manager, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                          std::function<void(error_t &&error)> &&done);

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

  struct attribute_index_t
  {
    int data;
  };

  [[nodiscard]] inline auto hash(std::pair<input_data_id_t, attribute_index_t> x) -> uint64_t
  {
    using namespace ankerl::unordered_dense::detail;
    uint64_t input;
    memcpy(&input, &x.first, sizeof(input));
    return wyhash::hash(wyhash::mix(input, uint64_t(x.second.data)));
  }
  struct hash_input_id_attribute_index_t
  {
    inline uint64_t operator()(const std::pair<input_data_id_t, attribute_index_t> &x) const
    {
      using namespace ankerl::unordered_dense::detail;
      uint64_t input;
      memcpy(&input, &x.first, sizeof(input));
      return wyhash::hash(wyhash::mix(input, uint64_t(x.second.data)));
    }
  };

  uv_fs_t _open_request{};
  event_pipe_t<error_t> &_cache_file_error;
  event_pipe_t<std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)>>> _write_event_pipe;
  event_pipe_t<std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)>>> _write_trees_pipe;
  event_pipe_t<serialized_tree_registry_t, std::function<void(storage_location_t, error_t &&error)>> _write_tree_registry_pipe;
  event_pipe_t<storage_location_t, std::vector<storage_location_t>, std::function<void(error_t &&error)>> _write_blob_locations_and_update_header_pipe;

  struct hash_storage_location_t
  {
    uint64_t operator()(const storage_location_t &a) const
    {
      std::pair<uint64_t, uint64_t> b;
      static_assert(sizeof(a) == sizeof(b), "hash function is invalid");
      memcpy(&b, &a, sizeof(b));
      b.first ^= b.second + 0x517cc1b727220a95 + (b.first << 6) + (b.first >> 2);
      return b.first;
    }
  };
  struct cache_item_impl_t
  {
    int ref;
    buffer_t buffer;
    std::shared_ptr<uint8_t[]> data;
  };

  std::mutex _mutex;

  ankerl::unordered_dense::map<storage_location_t, cache_item_impl_t, hash_storage_location_t> _cache_map;

  std::vector<std::unique_ptr<write_requests_t>> _write_requests;
  std::vector<std::unique_ptr<write_trees_request_t>> _write_trees_requests;
  std::vector<std::unique_ptr<write_tree_registry_request_t>> _write_tree_registry_requests;

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
    , cache_item(cache_file_handler.ref_points(location))
  {
    deserialize_points(cache_item.data, header, data, error);
  }
  ~read_only_points_t()
  {
    cache_file_handler.deref_points(location);
  }

  storage_handler_t &cache_file_handler;
  storage_location_t location;
  points_cache_item_t cache_item;
  storage_header_t header;
  buffer_t data;
  error_t error;
};

struct read_attribute_t
{
  read_attribute_t(storage_handler_t &cache_file_handler, storage_location_t location)
    : cache_file_handler(cache_file_handler)
    , location(location)
    , cache_item(cache_file_handler.ref_points(location))
    , data(cache_item.data)
  {
  }
  ~read_attribute_t()
  {
    cache_file_handler.deref_points(location);
  }

  storage_handler_t &cache_file_handler;
  storage_location_t location;
  points_cache_item_t cache_item;
  buffer_t data;
  error_t error;
};

struct read_points_with_data_t
{
  read_points_with_data_t(storage_handler_t &cache_file_handler, std::vector<storage_location_t> locations)
    : cache_file_handler(cache_file_handler)
    , locations(std::move(locations))
    , cache_items(this->locations.size())
    , data(this->locations.size())
  {
    for (auto &location : this->locations)
    {
      cache_items.push_back(cache_file_handler.ref_points(location));
    }
    deserialize_points(cache_items[0].data, header, data[0], error);
  }
  ~read_points_with_data_t()
  {
    for (auto &location : locations)
    {
      cache_file_handler.deref_points(location);
    }
  }
  storage_handler_t &cache_file_handler;
  std::vector<storage_location_t> locations;
  std::vector<points_cache_item_t> cache_items;
  storage_header_t header;
  std::vector<buffer_t> data;
  error_t error;
};

} // namespace converter
} // namespace points
