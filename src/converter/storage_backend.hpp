/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include <vio/event_loop.h>
#include <vio/task.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace points::converter
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

  // ---- bootstrap (constructed on / called from the processor thread) ----
  [[nodiscard]] virtual bool exists() const = 0;
  [[nodiscard]] virtual points_error_t open_for_write(bool truncate) = 0;
  [[nodiscard]] virtual points_error_t read_index(index_load_t &out) = 0;
  // Rebuild the packed allocator from its serialized blob; a no-op for object backends.
  [[nodiscard]] virtual points_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) = 0;

  // ---- data blobs (event-loop thread) ----
  // Reserve a location for `size` bytes. Synchronous, no IO. For packed this is register_blob and
  // may return a recycled offset (so the handler must invalidate its read cache before writing).
  virtual void allocate_blob(uint32_t size, storage_location_t &out) = 0;
  virtual vio::task_t<points_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) = 0;
  virtual vio::task_t<points_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) = 0;

  // ---- checkpoint / durability barrier (event-loop thread) ----
  // Writes the metadata blobs, then the index/manifest LAST, fsyncs, commits internal state, and
  // only then reclaims `freed`. On success the handler posts its index-written event.
  virtual vio::task_t<points_error_t> write_index(checkpoint_t checkpoint) = 0;
};

// Selects the backend from the URL scheme (no scheme / file:// -> packed; dir://, mem://, s3:// ...
// -> object). event_loop is the storage handler's own loop, used for all IO.
std::unique_ptr<storage_backend_t> create_storage_backend(const std::string &url, vio::event_loop_t &event_loop, points_error_t &error);

} // namespace points::converter
