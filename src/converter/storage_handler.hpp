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

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/operation/file.h>
#include <vio/operation/work.h>
#include <vio/task.h>
#include <vio/thread_pool.h>

#include "attributes_configs.hpp"
#include "blob_manager.hpp"
#include "compressor.hpp"
#include "conversion_types.hpp"
#include "error.hpp"
#include "lru_cache.hpp"
#include "perf_stats.hpp"
#include "tree.hpp"
#include <ankerl/unordered_dense.h>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>

#include <cstdint>

namespace points::converter
{

class storage_handler_t;

struct cache_key_t
{
  uint32_t file_id;
  uint64_t offset;

  bool operator==(const cache_key_t &other) const
  {
    return file_id == other.file_id && offset == other.offset;
  }
};

struct cache_key_hash_t
{
  uint64_t operator()(const cache_key_t &k) const
  {
    uint64_t h = uint64_t(k.file_id) ^ (k.offset * 0x9e3779b97f4a7c15ULL);
    return h;
  }
};

struct cache_value_t
{
  std::shared_ptr<uint8_t[]> compressed_data;
  uint32_t compressed_size;
};

struct compressed_write_data_t
{
  int buffer_index;
  std::shared_ptr<uint8_t[]> data;
  uint32_t size;
  std::string attribute_name;
  point_format_t format;
  uint32_t uncompressed_size;
  double min_value = std::numeric_limits<double>::max();
  double max_value = std::numeric_limits<double>::lowest();
  bool is_lod = false;
};

struct read_request_t
{
  void wait_for_read();

  std::shared_ptr<uint8_t[]> buffer;
  buffer_t buffer_info;
  error_t error;

  bool _done = false;
  std::mutex _mutex;
  std::condition_variable _block_for_read;
};

class storage_handler_t
{
public:
  storage_handler_t(const std::string &url, vio::thread_pool_t &thread_pool, attributes_configs_t &attributes_configs, perf_stats_t &perf_stats, vio::event_pipe_t<void> &index_written, vio::event_pipe_t<error_t> &storage_error_pipe, error_t &error);
  ~storage_handler_t();
  [[nodiscard]] bool file_exists() const
  {
    return _file_exists;
  }
  [[nodiscard]] error_t read_index(std::unique_ptr<uint8_t[]> &free_blobs_buffer, uint32_t &free_blobs_size, std::unique_ptr<uint8_t[]> &attribute_configs_buffer, uint32_t &attribute_configs_size,
                                   std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t &tree_registry_size);
  [[nodiscard]] error_t deserialize_free_blobs(const std::unique_ptr<uint8_t[]> &data, uint32_t size);
  [[nodiscard]] error_t upgrade_to_write(bool truncate);

  void write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
             std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)> done);
  void write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)> done);
  void write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done);
  void write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> done);

  std::shared_ptr<read_request_t> read(storage_location_t location);

  void set_compressor(compression_method_t method);
  void set_read_cache_size(uint64_t max_bytes);
  void set_on_write_progress(std::function<void()> cb) { _on_write_progress = std::move(cb); }

  const compression_stats_t &get_compression_stats() const { return _compression_stats; }

private:
  void handle_write_events(
    std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)>> &&event);
  void handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)>> &&event);
  void handle_write_tree_registry(serialized_tree_registry_t &&serialized_trr, std::function<void(storage_location_t, error_t &&error)> &&done);
  void handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(error_t &&error)> &&done);
  void handle_write_index(free_blob_manager_t &&new_blob_manager, const storage_location_t &free_blobs, const storage_location_t &attribute_configs, const storage_location_t &tree_registry,
                          const storage_location_t &compression_stats, const storage_location_t &perf_stats, std::function<void(error_t &&error)> &&done);
  void handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location);

  vio::task_t<void> do_write(const std::shared_ptr<uint8_t[]> &data, const storage_location_t &location);
  vio::task_t<void> do_write_events(storage_header_t header, attributes_id_t attributes_id, attribute_buffers_t attribute_buffers,
                                    std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const error_t &error)> done);
  vio::task_t<void> do_write_trees(std::vector<tree_id_t> tree_ids, std::vector<serialized_tree_t> serialized_trees,
                                   std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&)> done);
  vio::task_t<void> do_write_tree_registry(serialized_tree_registry_t serialized_tree_registry, std::function<void(storage_location_t, error_t &&error)> done);
  vio::task_t<void> do_write_blob_locations_and_update_header(storage_location_t new_tree_registry_location, std::vector<storage_location_t> old_locations, std::function<void(error_t &&error)> done);
  vio::task_t<void> do_write_index(free_blob_manager_t new_blob_manager, storage_location_t free_blobs, storage_location_t attribute_configs, storage_location_t tree_registry,
                                   storage_location_t compression_stats, storage_location_t perf_stats, std::function<void(error_t &&error)> done);
  vio::task_t<void> do_read_request(std::shared_ptr<read_request_t> read_request, storage_location_t location);

  std::string _file_name;
  vio::thread_pool_t &_thread_pool;
  std::unique_ptr<compressor_t> _compressor;
  vio::thread_with_event_loop_t _event_loop_thread;
  vio::event_loop_t &_event_loop;
  std::optional<vio::auto_close_file_t> _file;
  std::atomic_bool _file_opened;
  std::atomic_bool _file_exists;
  std::atomic_bool _file_opened_in_write_mode;

  attributes_configs_t &_attributes_configs;
  uint32_t _serialized_index_size;
  free_blob_manager_t _blob_manager;

  storage_location_t _attributes_location;
  storage_location_t _blobs_location;
  storage_location_t _stats_location;
  storage_location_t _perf_stats_location;

  perf_stats_t &_perf_stats;
  std::function<void()> _on_write_progress;
  compression_stats_t _compression_stats;
  std::set<uint32_t> _seen_input_files;

  vio::event_pipe_t<void> &_index_written;
  vio::event_pipe_t<error_t> &_storage_error;
  vio::event_pipe_t<std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const error_t &error)>>> _write_event_pipe;
  vio::event_pipe_t<std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, error_t &&error)>>> _write_trees_pipe;
  vio::event_pipe_t<serialized_tree_registry_t, std::function<void(storage_location_t, error_t &&error)>> _write_tree_registry_pipe;
  vio::event_pipe_t<storage_location_t, std::vector<storage_location_t>, std::function<void(error_t &&error)>> _write_blob_locations_and_update_header_pipe;
  vio::event_pipe_t<std::shared_ptr<read_request_t>, storage_location_t> _read_request_pipe;

  lru_cache_t<cache_key_t, cache_value_t, cache_key_hash_t> _read_cache;

  std::mutex _mutex;
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
  read_only_points_t(storage_handler_t &storage_handler, storage_location_t a_location)
    : location(a_location)
    , read_request(storage_handler.read(location))
  {
    read_request->wait_for_read();
    deserialize_points(read_request->buffer_info, header, data, error);
  }
  ~read_only_points_t()
  {
  }

  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  storage_header_t header;
  buffer_t data;
  error_t error;
};

struct read_attribute_t
{
  read_attribute_t(storage_handler_t &a_storage_handler, storage_location_t a_location)
    : storage_handler(a_storage_handler)
    , location(a_location)
    , read_request(a_storage_handler.read(a_location))
  {
    read_request->wait_for_read();
    data = read_request->buffer_info;
  }

  storage_handler_t &storage_handler;
  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  buffer_t data;
  error_t error;
};

} // namespace points::converter
