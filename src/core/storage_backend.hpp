/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include "dataset_types.hpp"
#include "error.hpp"

#include <vio/event_loop.h>
#include <vio/task.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dew::core
{

// The five metadata regions read from the dataset index on open. free_blobs/attribute_configs/
// tree_registry raw buffers are handed up to the processor; stats/perf are deserialized by the
// storage handler. A backend also records internally the locations it must free at the next checkpoint.
struct index_load_t
{
  std::unique_ptr<uint8_t[]> free_blobs;
  uint32_t free_blobs_size = 0;
  std::unique_ptr<uint8_t[]> attribute_configs;
  uint32_t attribute_configs_size = 0;
  std::unique_ptr<uint8_t[]> tree_registry;
  uint32_t tree_registry_size = 0;
  std::unique_ptr<uint8_t[]> stats;
  uint32_t stats_size = 0;
  std::unique_ptr<uint8_t[]> perf;
  uint32_t perf_size = 0;
};

// Everything a checkpoint needs. The tree registry blob is already written (via write_allocated) and
// its location passed here. `freed` are old data blobs (e.g. superseded tree blobs) to reclaim. The
// backend allocates + writes the attribute-configs/stats/perf payloads, writes the index/manifest
// last, and only then reclaims freed blobs — preserving the "index written strictly last" invariant.
struct checkpoint_t
{
  storage_location_t tree_registry;
  std::vector<storage_location_t> freed;
  std::shared_ptr<uint8_t[]> attribute_configs;
  uint32_t attribute_configs_size = 0;
  std::shared_ptr<uint8_t[]> stats;
  uint32_t stats_size = 0;
  std::shared_ptr<uint8_t[]> perf;
  uint32_t perf_size = 0;
};

// The storage transport/layout seam the storage_handler talks to. Two implementations:
//  - packed_file_backend: one file, free_blob_manager offsets, in-place superblock + fsync.
//  - object_backend: one object per blob (dir / S3 / in-memory) via an io_manager_t.
// The handler keeps compression, the LRU read cache, perf timers, event pipes, and its public API;
// the backend owns the file/allocator/index and the durability barrier.
struct storage_backend_t
{
  virtual ~storage_backend_t() = default;

  // Type tag (no RTTI): true for the local packed-file backend, which is the only one that hosts
  // the cache tier (residency/spill/eviction).
  virtual bool is_packed_file() const
  {
    return false;
  }

  // ---- bootstrap (constructed on / called from the processor thread) ----
  [[nodiscard]] virtual bool exists() const = 0;
  // Why exists() is false when the answer is not a clean "absent": a remote existence probe can FAIL
  // (wrong region, missing credentials, network) rather than return 404. Empty when absent-or-unknown.
  [[nodiscard]] virtual std::string exists_error() const
  {
    return {};
  }
  [[nodiscard]] virtual dew_error_t open_for_write(bool truncate) = 0;
  [[nodiscard]] virtual dew_error_t read_index(index_load_t &out) = 0;
  // Coroutine form, for callers that must not block -- the query engine's open path, and anything
  // running on a cooperative loop where blocking would stall the whole program.
  //
  // The default simply forwards to the synchronous version, which is right for the packed local file
  // (its index read is a handful of synchronous libuv fs calls, and it does not exist under wasm at
  // all). The object store overrides it, because that is where a blocking index read costs a network
  // round trip and, under wasm, would need ASYNCIFY.
  virtual vio::task_t<dew_error_t> read_index_async(index_load_t &out) { co_return read_index(out); }
  // Rebuild the packed allocator from its serialized blob; a no-op for object backends.
  [[nodiscard]] virtual dew_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) = 0;

  // ---- data blobs (event-loop thread) ----
  // What a blob holds, from the storage tier's perspective. `data` blobs (point/attribute payloads)
  // are candidates for upload/eviction/spill in the cached backend; `metadata` blobs (trees,
  // registry, stats, residency table) are pinned local forever -- they are rewritten every
  // checkpoint and reclaimed through the existing checkpoint `freed` path instead.
  enum class blob_kind_t : uint8_t
  {
    data,
    metadata,
  };
  // Reserve a location for `size` bytes. Synchronous, no IO. For packed this is register_blob and
  // may return a recycled offset (so the handler must invalidate its read cache before writing).
  virtual void allocate_blob(uint32_t size, blob_kind_t kind, storage_location_t &out) = 0;
  virtual vio::task_t<dew_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) = 0;
  virtual vio::task_t<dew_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) = 0;

  // ---- checkpoint / durability barrier (event-loop thread) ----
  // Writes the metadata blobs, then the index/manifest LAST, fsyncs, commits internal state, and
  // only then reclaims `freed`. On success the handler posts its index-written event.
  virtual vio::task_t<dew_error_t> write_index(checkpoint_t checkpoint) = 0;

  // ---- cache-tier pressure hooks (event-loop thread; no-ops outside the cached local backend) ----
  // Reclaim local bytes if the tier is over pressure and durable victims exist.
  virtual void maybe_evict()
  {
  }
  // True when only a checkpoint can relieve pressure (pending remote facts need the durable flip).
  virtual bool wants_checkpoint() const
  {
    return false;
  }
  // The uploader committed this blob to the destination's final layout: remote_id encodes
  // (pack_id << 32) | in-pack offset. Makes the blob evictable once a checkpoint commits the fact.
  virtual void note_blob_uploaded(uint64_t offset, uint32_t size, uint64_t remote_id)
  {
    (void)offset;
    (void)size;
    (void)remote_id;
  }
};

// Selects the backend from the URL scheme (no scheme / file:// -> packed; dir://, mem://, s3:// ...
// -> object). event_loop is the storage handler's own loop, used for all IO. `connection` is a vendor
// connection string (see vio connection_string.h) supplying credentials/endpoint/region for cloud stores;
// empty means "AWS_*/AZURE_* environment + defaults". Ignored for local packed files.
std::unique_ptr<storage_backend_t> create_storage_backend(const std::string &url, std::string_view connection, vio::event_loop_t &event_loop, dew_error_t &error);
std::unique_ptr<storage_backend_t> create_storage_backend(const std::string &url, vio::event_loop_t &event_loop, dew_error_t &error);

} // namespace dew::core
