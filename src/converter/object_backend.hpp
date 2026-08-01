/************************************************************************
** dewfall - point cloud management software.
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

#include "index_format.hpp"
#include "storage_backend.hpp"

#include <vio/objstore/object_store.h>

#include <memory>
#include <mutex>
#include <string>

namespace dew::converter
{

// Object storage over an io_manager_t (directory / in-memory / S3). Two on-bucket layouts share the
// "manifest" object name and are discriminated by content at read_index time:
//
//  - legacy (writable): object-per-blob, each blob one object named from its id (a monotonically
//    allocated 64-bit counter split across file_id/offset). The index is a 128-byte 'DEW\0'
//    superblock written last (atomic replace) per checkpoint; freed objects are removed only after
//    the manifest is durable.
//  - DEW2 (read-only here): the incremental-upload layout (see bucket_format.hpp). The manifest is
//    a 256-byte 'DEW2' root manifest; storage_location_t is verbatim pack addressing (file_id =
//    pack id -> object "data/{file_id:08x}", offset = byte offset inside the pack), so every blob
//    read is one ranged GET. Written by upload_handler_t / dew_copy, never through this backend.
class object_backend_t : public storage_backend_t
{
public:
  object_backend_t(std::unique_ptr<vio::objstore::io_manager_t> io, vio::event_loop_t &event_loop);
  ~object_backend_t() override;

  [[nodiscard]] bool exists() const override;
  [[nodiscard]] dew_error_t open_for_write(bool truncate) override;
  [[nodiscard]] dew_error_t read_index(index_load_t &out) override;
  [[nodiscard]] dew_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) override;
  void allocate_blob(uint32_t size, blob_kind_t kind, storage_location_t &out) override;
  vio::task_t<dew_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) override;
  vio::task_t<dew_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) override;
  vio::task_t<dew_error_t> write_index(checkpoint_t checkpoint) override;

  static constexpr const char *k_manifest_name = "manifest";
  // The object name is derived from BOTH storage_location fields, so the blob id space is the full
  // 64-bit counter split across file_id (low 32 bits) and offset (high bits) — far past file_id's 4B.
  static std::string object_name(uint32_t file_id, uint64_t offset);

private:
  vio::task_t<dew_error_t> do_read_index(index_load_t &out);
  vio::task_t<dew_error_t> read_location(storage_location_t loc, std::unique_ptr<uint8_t[]> &buf, uint32_t &size);
  vio::task_t<dew_error_t> probe_exists(bool &out); // HEAD the manifest to set _exists on open
  storage_location_t next_location(uint32_t size); // allocate a fresh 64-bit id split into file_id/offset

  std::unique_ptr<vio::objstore::io_manager_t> _io;
  vio::event_loop_t &_event_loop;
  bool _exists = false;
  bool _dew2 = false;          // layout sniffed from the manifest content in do_read_index
  uint64_t _manifest_size = 0; // from the open-time HEAD; discriminates 128 (legacy) vs 256 (DEW2)
  uint64_t _next_id = 0;

  // Locations of the previous committed metadata objects, reclaimed after the next manifest commit.
  storage_location_t _attributes_location;
  storage_location_t _stats_location;
  storage_location_t _perf_stats_location;
  storage_location_t _tree_registry_location; // tracked for a future gc(); not freed (matches packed)

  std::mutex _mutex; // guards _next_id
};

} // namespace dew::converter
