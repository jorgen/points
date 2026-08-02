/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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

// Incremental uploader: ships finalized subtrees ("bands" -- tree SETS, not morton intervals) from
// the local cache to the destination bucket in the DEW2 layout. Bands derive exclusively from
// COMMITTED checkpoints: the tree handler emits a band job after each successful checkpoint commit,
// listing the newly-final trees by their (immutable) cache blob locations. This handler then, on
// its own event loop:
//
//   1. reads each tree blob from the cache, walks its storage map deterministically,
//   2. reads every not-yet-uploaded data blob (compressed bytes verbatim, raw reads) and PUTs
//      each as its own immutable object data/{id:08x} (whole-object reads, no ranges),
//   3. remaps the trees' storage maps to the bucket object locations and uploads the trees too,
//   4. PUTs: data objects -> band manifest -> root manifest (the ONLY mutable object; bands commit
//      strictly in order),
//   5. reports the commit: the cache tier marks the blobs uploaded (-> evictable after their next
//      checkpoint), the tree registry marks the trees uploaded.
//
// The terminal band (all-0xFF watermark) additionally uploads the registry (locations remapped to
// bucket objects) + attributes and flips the root manifest to complete=1 -- after which the bucket
// reads exactly like any dataset: manifest -> registry -> trees -> blobs.
//
// Retry: each PUT retries with exponential backoff; on exhaustion the uploader parks (conversion
// continues locally; a later resume re-derives the band from the committed cache state).
// Upload applies NO backpressure upstream -- the cache absorbs the backlog; disk pressure is the
// cache tier's domain.

#include "bucket_format.hpp"
#include "storage_handler.hpp"
#include "tree.hpp"

#include <vio/event_loop.h>
#include <vio/event_pipe.h>
#include <vio/objstore/object_store.h>
#include <vio/task.h>
#include <vio/thread_pool.h>

#include <ankerl/unordered_dense.h>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dew::converter
{

struct band_job_t
{
  uint32_t band_id = 0;
  morton::morton192_t watermark = {};
  bool terminal = false;
  // Newly-final trees: id + the (immutable) cache location of their serialized blob.
  std::vector<std::pair<uint32_t, storage_location_t>> trees;
  // Serialized attributes_configs at job time (band manifests embed it for partial readers).
  std::vector<uint8_t> attributes_snapshot;
  // Terminal only: the full registry snapshot (v2) whose locations the uploader remaps to objects.
  serialized_tree_registry_t registry_snapshot = {};
};

struct upload_stats_t
{
  uint64_t bytes_uploaded = 0;
  uint32_t bands_committed = 0;
  uint32_t objects_written = 0;
  bool parked = false; // retries exhausted; resume later
  bool complete = false;
};

class upload_handler_t
{
public:
  // `io` must be rooted at the destination dataset prefix (the same store the cache tier spills
  // to). `pool` parks blocking cache-read waits. Callbacks fire on the uploader loop.
  upload_handler_t(std::unique_ptr<vio::objstore::io_manager_t> io, storage_handler_t &storage, vio::thread_pool_t &pool, const uint8_t (&dataset_uuid)[16]);
  // Create the io_manager from a destination URL + connection string, bound to the uploader's own
  // loop. On failure `error` is set and the handler must not be used.
  upload_handler_t(const std::string &destination_url, const std::string &connection, storage_handler_t &storage, vio::thread_pool_t &pool, const uint8_t (&dataset_uuid)[16], dew_error_t &error);
  ~upload_handler_t();

  // Reconcile with an existing bucket (resume): reads the root manifest + band manifests, rebuilds
  // the dedup map / tree table / next ids. Fresh bucket -> writes an empty root manifest with our
  // uuid. A uuid mismatch is an error (the bucket belongs to a different cache generation).
  // Blocking (bootstrap-time); call before any enqueue_band.
  [[nodiscard]] dew_error_t bootstrap();

  // Which trees are already uploaded (from bootstrap), with their band -- the processor seeds the
  // registry's tree_state/tree_band from this (the bucket is authoritative).
  const std::vector<std::pair<uint32_t, uint32_t>> &uploaded_tree_bands() const { return _bootstrap_tree_bands; }
  uint32_t committed_band_count() const { return _band_count; }

  // Thread-safe; bands must be enqueued in ascending band_id order.
  void enqueue_band(band_job_t &&job);

  // set BEFORE the first enqueue. on_band_committed fires after the root manifest lands.
  void set_on_band_committed(std::function<void(uint32_t band_id, std::vector<uint32_t> tree_ids, const morton::morton192_t &watermark)> cb) { _on_band_committed = std::move(cb); }
  void set_on_error(std::function<void(const dew_error_t &, bool parked)> cb) { _on_error = std::move(cb); }

  upload_stats_t stats() const
  {
    std::unique_lock<std::mutex> lock(_stats_mutex);
    return _stats;
  }
  // True when every enqueued band has committed (or the uploader parked).
  bool drained() const;
  // Block until drained (wait_idle integration). Returns immediately if parked.
  void wait_drained();

  void stop();

private:
  void handle_band(band_job_t &&job);
  vio::task_t<void> process_band(band_job_t job);
  vio::task_t<dew_error_t> put_with_retry(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size);
  // Bounded window of concurrent data-object PUTs (uploader-loop only; coroutines interleave at
  // co_await points, no threads involved). process_band launches puts through it and must drain
  // it (wait_for_room(1)) before ANY exit path -- the detached puts reference the window.
  struct put_window_t
  {
    int in_flight = 0;
    dew_error_t first_error = {};
    std::coroutine_handle<> waiter = {};
    struct room_awaiter_t
    {
      put_window_t &window;
      int limit;
      [[nodiscard]] bool await_ready() const
      {
        return window.in_flight < limit;
      }
      void await_suspend(std::coroutine_handle<> handle)
      {
        assert(!window.waiter && "one process_band at a time");
        window.waiter = handle;
      }
      void await_resume() const
      {
      }
    };
    // Suspend until fewer than `limit` puts are in flight (limit 1 == fully drained).
    [[nodiscard]] room_awaiter_t wait_for_room(int limit)
    {
      return room_awaiter_t{*this, limit};
    }
  };
  // PUT one data object data/{object_id:08x} whose content is exactly `bytes` (copied here).
  vio::detached_task_t put_data_object_windowed(put_window_t *window, uint32_t object_id, std::shared_ptr<uint8_t[]> data, uint64_t size);
  // Read a cache blob's raw bytes, parking the wait on the pool so the uploader loop stays free.
  vio::task_t<dew_error_t> read_cache_blob(storage_location_t location, std::vector<uint8_t> &out);

  vio::thread_with_event_loop_t _loop_thread;
  vio::event_loop_t &_loop;
  std::unique_ptr<vio::objstore::io_manager_t> _io;
  storage_handler_t &_storage;
  vio::thread_pool_t &_pool;
  uint8_t _uuid[16];

  vio::event_pipe_t<band_job_t> _band_pipe;

  // Uploader-loop state.
  ankerl::unordered_dense::map<uint64_t, storage_location_t> _dedup; // cache offset -> bucket location
  ankerl::unordered_dense::map<uint32_t, storage_location_t> _tree_locations;
  uint32_t _next_object_id = 0;
  uint32_t _band_count = 0;
  bool _parked = false;
  bool _band_in_flight = false;
  std::vector<band_job_t> _queued;

  std::vector<std::pair<uint32_t, uint32_t>> _bootstrap_tree_bands; // (tree_id, band_id)

  std::function<void(uint32_t, std::vector<uint32_t>, const morton::morton192_t &)> _on_band_committed;
  std::function<void(const dew_error_t &, bool)> _on_error;

  mutable std::mutex _stats_mutex;
  std::condition_variable _drained_cv;
  upload_stats_t _stats;
  uint32_t _enqueued_bands = 0;
  uint32_t _bootstrap_band_count = 0; // bands that were already committed when we bootstrapped
};

} // namespace dew::converter
