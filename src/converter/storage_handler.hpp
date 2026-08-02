/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#ifndef __EMSCRIPTEN__
#include <vio/operation/file.h> // pulls libuv; the storage handler drives IO through the backend, not vio file ops
#endif
#include <vio/operation/work.h>
#include <vio/task.h>
#include <vio/thread_pool.h>

#include "attributes_configs.hpp"
#include "compressor.hpp"
#include "conversion_types.hpp"
#include "error.hpp"
#include "lru_cache.hpp"
#include "perf_stats.hpp"
#include "storage_backend.hpp"
#include "tree.hpp"
#include <ankerl/unordered_dense.h>

#include <atomic>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <set>

#include <cstdint>

namespace dew::converter
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

struct decompressed_cache_value_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size;
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
  void set_cancelled() { _cancelled.store(true, std::memory_order_relaxed); }
  bool is_cancelled() const { return _cancelled.load(std::memory_order_relaxed); }

  std::shared_ptr<uint8_t[]> buffer;
  dew_converter_buffer_t buffer_info;
  dew_error_t error;

  bool raw = false; // when set, read() returns the COMPRESSED bytes as-is (no decompress) -- used by the
                    // wasm decode-worker path, which decompresses off the main thread.
  bool _done = false;
  std::atomic_bool _cancelled{false};
  std::mutex _mutex;
  std::condition_variable _block_for_read;

#ifdef __EMSCRIPTEN__
  // Single-thread cooperative build: rather than parking a thread on wait_for_read, a coroutine can
  // co_await the read completing. do_read_request resumes this continuation on its owning loop when the
  // read finishes, so nothing blocks. (read() runs on the storage loop; the awaiting coroutine may live
  // on another cooperative loop, hence the explicit continuation loop for a cross-loop resume.)
  std::coroutine_handle<> _continuation{};
  vio::event_loop_t *_continuation_loop = nullptr;
  struct awaiter_t
  {
    read_request_t *req;
    vio::event_loop_t *loop;
    bool await_ready() const noexcept { return req->_done; }
    void await_suspend(std::coroutine_handle<> h) noexcept
    {
      req->_continuation = h;
      req->_continuation_loop = loop;
    }
    void await_resume() const noexcept {}
  };
  awaiter_t await_on(vio::event_loop_t &loop) noexcept { return awaiter_t{this, &loop}; }
#endif
};

class storage_handler_t
{
public:
  storage_handler_t(const std::string &url, vio::thread_pool_t &thread_pool, attributes_configs_t &attributes_configs, perf_stats_t &perf_stats, vio::event_pipe_t<void> &index_written, vio::event_pipe_t<dew_error_t> &storage_error_pipe, dew_error_t &error);
  ~storage_handler_t();
  // Join the storage loop thread. Idempotent. Called by the processor's ordered teardown (and the destructor)
  // before _backend / _read_cache / the event pipes an in-flight read touches are destroyed.
  void stop_loop();
  [[nodiscard]] bool file_exists() const
  {
    return _backend && _backend->exists();
  }
  [[nodiscard]] dew_error_t read_index(std::unique_ptr<uint8_t[]> &free_blobs_buffer, uint32_t &free_blobs_size, std::unique_ptr<uint8_t[]> &attribute_configs_buffer, uint32_t &attribute_configs_size,
                                   std::unique_ptr<uint8_t[]> &tree_registry_buffer, uint32_t &tree_registry_size);
  [[nodiscard]] dew_error_t deserialize_free_blobs(const std::unique_ptr<uint8_t[]> &data, uint32_t size);
  [[nodiscard]] dew_error_t upgrade_to_write(bool truncate);

  void write(const storage_header_t &header, attributes_id_t attributes_id, attribute_buffers_t &&buffers,
             std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const dew_error_t &error)> done);
  void write_trees(std::vector<tree_id_t> &&tree_ids, std::vector<serialized_tree_t> &&serialized_trees, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, dew_error_t &&error)> done);
  void write_tree_registry(serialized_tree_registry_t &&serialized_tree_registry, std::function<void(storage_location_t, dew_error_t &&error)> done);
  void write_blob_locations_and_update_header(storage_location_t location, std::vector<storage_location_t> &&old_locations, std::function<void(dew_error_t &&error)> done);

  // decompress_inline: on a RAM-cache hit of a compressed blob the decompress normally hops to the
  // shared thread pool (a render-thread latency fix). Callers that ALREADY run on a pool worker and
  // synchronously wait_for_read (LOD + collapse readers) MUST pass true: with every pool thread
  // parked in such a wait, the hopped decompress queues behind the waiters and the pool deadlocks.
  std::shared_ptr<read_request_t> read(storage_location_t location, bool raw = false, bool decompress_inline = false);

  void register_input_file_size(uint32_t file_id, uint64_t size_bytes);
  void set_compressor(compression_method_t method);
  void set_compression_level(int level);
  void set_read_cache_size(uint64_t max_bytes);
  void set_decompressed_cache_size(uint64_t max_bytes);
  uint64_t read_cache_current_bytes();
  void set_on_write_progress(std::function<void()> cb) { _on_write_progress = std::move(cb); }
  // Cache-tier pressure: invoked (from the storage loop) at most once per arm when the backend
  // reports that only a checkpoint can relieve pressure. rearm after the checkpoint completes.
  void set_checkpoint_request_callback(std::function<void()> cb) { _on_checkpoint_request = std::move(cb); }
  void rearm_checkpoint_request() { _checkpoint_requested.store(false, std::memory_order_release); }
  // Upload tier: a band committed; mark its blobs uploaded in the cache tier (thread-safe; hops to
  // the storage loop). Each pair is (cache blob offset, bucket pack location).
  void note_blobs_uploaded(std::vector<std::pair<uint64_t, storage_location_t>> &&blobs);
  // Block until every already-posted storage-loop task has run (FIFO barrier). NOT wasm-safe
  // (single-threaded); destination-mode teardown only.
  void drain_posted_events();

  // ---- cache-tier configuration (destination mode; requires a LOCAL packed cache file) ----
  // Enable residency tracking (cap 0 = track only) and, when destination_url is non-empty, the
  // spill area + destination reads on that bucket. Call at bootstrap, before read_index/open.
  [[nodiscard]] dew_error_t configure_cache_tier(uint64_t cap_bytes, const std::string &destination_url, const std::string &connection);
  // Runtime cap adjustment (thread-safe; lowering triggers a reclaim pass on the storage loop).
  void set_cache_max_bytes(uint64_t cap_bytes);
  // Returns the dataset uuid, generating + installing a fresh one if the cache has none yet.
  // Only valid in destination mode (packed backend).
  void ensure_dataset_uuid(uint8_t (&out)[16]);
  // After read_index/restore_allocator at bootstrap: replay spill liveness + GC orphan segments.
  [[nodiscard]] dew_error_t run_spill_bootstrap();
  // Mark the next checkpoint as the orderly-teardown one (keeps local_* residency on reopen).
  void set_clean_shutdown_next_checkpoint();
  struct cache_tier_stats_t
  {
    uint64_t resident_bytes = 0;
    uint64_t cap_bytes = 0;
    uint32_t tracked_blobs = 0;
    uint64_t spilled_bytes = 0;
  };
  // Approximate (unsynchronized counters); false when no cache tier is configured.
  bool get_cache_tier_stats(cache_tier_stats_t &out) const;

  const compression_stats_t &get_compression_stats() const { return _compression_stats; }
  const perf_stats_t::deserialized_perf_stats_t &get_deserialized_perf_stats() const { return _deserialized_perf_stats; }

private:
  void handle_write_events(
    std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const dew_error_t &error)>> &&event);
  void handle_write_trees(std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, dew_error_t &&)>> &&event);
  void handle_write_tree_registry(serialized_tree_registry_t &&serialized_trr, std::function<void(storage_location_t, dew_error_t &&error)> &&done);
  void handle_write_blob_locations_and_update_header(storage_location_t &&new_tree_registry_location, std::vector<storage_location_t> &&old_locations, std::function<void(dew_error_t &&error)> &&done);
  void handle_read_request(std::shared_ptr<read_request_t> &&read_request, storage_location_t &&location);

  vio::task_t<void> do_write(const std::shared_ptr<uint8_t[]> &data, const storage_location_t &location);
  vio::task_t<void> do_write_events(storage_header_t header, attributes_id_t attributes_id, attribute_buffers_t attribute_buffers,
                                    std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t> &&, const dew_error_t &error)> done);
  vio::task_t<void> do_write_trees(std::vector<tree_id_t> tree_ids, std::vector<serialized_tree_t> serialized_trees,
                                   std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, dew_error_t &&)> done);
  vio::task_t<void> do_write_tree_registry(serialized_tree_registry_t serialized_tree_registry, std::function<void(storage_location_t, dew_error_t &&error)> done);
  vio::task_t<void> do_write_blob_locations_and_update_header(storage_location_t new_tree_registry_location, std::vector<storage_location_t> old_locations, std::function<void(dew_error_t &&error)> done);
  vio::task_t<void> do_read_request(std::shared_ptr<read_request_t> read_request, storage_location_t location);

  vio::thread_pool_t &_thread_pool;
  std::unique_ptr<compressor_t> _compressor;
  vio::thread_with_event_loop_t _event_loop_thread;
  vio::event_loop_t &_event_loop;
  std::unique_ptr<storage_backend_t> _backend;
  std::atomic<int> _reads_in_flight{0}; // do_read_request coroutines currently holding the backend/a connection

  attributes_configs_t &_attributes_configs;

  perf_stats_t &_perf_stats;
  std::function<void()> _on_write_progress;
  std::function<void()> _on_checkpoint_request;
  std::atomic<bool> _checkpoint_requested{false};
  compression_stats_t _compression_stats;
  perf_stats_t::deserialized_perf_stats_t _deserialized_perf_stats{};
  std::set<uint32_t> _seen_input_files;
  ankerl::unordered_dense::map<uint32_t, uint64_t> _input_file_sizes;

  vio::event_pipe_t<void> &_index_written;
  vio::event_pipe_t<dew_error_t> &_storage_error;
  vio::event_pipe_t<std::tuple<storage_header_t, attributes_id_t, attribute_buffers_t, std::function<void(const storage_header_t &, attributes_id_t, std::vector<storage_location_t>, const dew_error_t &error)>>> _write_event_pipe;
  vio::event_pipe_t<std::tuple<std::vector<tree_id_t>, std::vector<serialized_tree_t>, std::function<void(std::vector<tree_id_t> &&, std::vector<storage_location_t> &&, dew_error_t &&error)>>> _write_trees_pipe;
  vio::event_pipe_t<serialized_tree_registry_t, std::function<void(storage_location_t, dew_error_t &&error)>> _write_tree_registry_pipe;
  vio::event_pipe_t<storage_location_t, std::vector<storage_location_t>, std::function<void(dew_error_t &&error)>> _write_blob_locations_and_update_header_pipe;
  vio::event_pipe_t<std::shared_ptr<read_request_t>, storage_location_t> _read_request_pipe;

  lru_cache_t<cache_key_t, cache_value_t, cache_key_hash_t> _read_cache;
  // Decompressed-side cache for the pool readers (LOD sampling, leaf splits, collapse merges):
  // they re-read the same big ingest chunks many times, and re-inflating a 64MB chunk per read
  // dominates conversion time. Populated only on the decompress_inline path.
  lru_cache_t<cache_key_t, decompressed_cache_value_t, cache_key_hash_t> _decompressed_cache;

  std::mutex _mutex;
};

// deserialize_points moved to conversion_types.hpp (storage-free) so the decode path / a decode worker can
// use it without pulling this header.

struct read_only_points_t
{
  read_only_points_t(storage_handler_t &storage_handler, storage_location_t a_location)
    : location(a_location)
    , read_request(storage_handler.read(location, /*raw=*/false, /*decompress_inline=*/true))
  {
    read_request->wait_for_read();
    // A failed read leaves data/header empty; callers MUST check `error` before dereferencing.
    error = read_request->error;
    if (error.code != 0)
      return;
    deserialize_points(read_request->buffer_info, header, data, error);
  }
  ~read_only_points_t()
  {
  }

  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  storage_header_t header;
  dew_converter_buffer_t data;
  dew_error_t error;
};

struct read_attribute_t
{
  read_attribute_t(storage_handler_t &a_storage_handler, storage_location_t a_location)
    : storage_handler(a_storage_handler)
    , location(a_location)
    , read_request(a_storage_handler.read(a_location, /*raw=*/false, /*decompress_inline=*/true))
  {
    read_request->wait_for_read();
    // A failed read leaves data empty; callers MUST check `error` before dereferencing.
    error = read_request->error;
    if (error.code != 0)
      return;
    data = read_request->buffer_info;
  }

  storage_handler_t &storage_handler;
  storage_location_t location;
  std::shared_ptr<read_request_t> read_request;
  dew_converter_buffer_t data;
  dew_error_t error;
};

} // namespace dew::converter
