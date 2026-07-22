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

#include "index_format.hpp"
#include "io_manager.hpp"
#include "storage_backend.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace points::converter
{

// Object-per-blob storage over an io_manager_t (directory / in-memory / later S3). Each blob is one
// object named from its id (storage_location_t.file_id, allocated monotonically). The dataset index
// is a single "manifest" object using the same 128-byte layout as the packed superblock, written last
// (atomic replace) on every checkpoint. Preserves the "index written strictly last" crash-safety
// invariant: freed objects are removed only after the manifest is durable.
class object_backend_t : public storage_backend_t
{
public:
  object_backend_t(std::unique_ptr<io_manager_t> io, vio::event_loop_t &event_loop);
  ~object_backend_t() override;

  [[nodiscard]] bool exists() const override;
  [[nodiscard]] points_error_t open_for_write(bool truncate) override;
  [[nodiscard]] points_error_t read_index(index_load_t &out) override;
  [[nodiscard]] points_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) override;
  void allocate_blob(uint32_t size, storage_location_t &out) override;
  vio::task_t<points_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) override;
  vio::task_t<points_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) override;
  vio::task_t<points_error_t> write_index(checkpoint_t checkpoint) override;

  static constexpr const char *k_manifest_name = "manifest";
  // The object name is derived from BOTH storage_location fields, so the blob id space is the full
  // 64-bit counter split across file_id (low 32 bits) and offset (high bits) — far past file_id's 4B.
  static std::string object_name(uint32_t file_id, uint64_t offset);

private:
  vio::task_t<points_error_t> do_read_index(index_load_t &out);
  vio::task_t<points_error_t> read_location(storage_location_t loc, std::unique_ptr<uint8_t[]> &buf, uint32_t &size);
  storage_location_t next_location(uint32_t size); // allocate a fresh 64-bit id split into file_id/offset

  std::unique_ptr<io_manager_t> _io;
  vio::event_loop_t &_event_loop;
  bool _exists = false;
  uint64_t _next_id = 0;

  // Locations of the previous committed metadata objects, reclaimed after the next manifest commit.
  storage_location_t _attributes_location;
  storage_location_t _stats_location;
  storage_location_t _perf_stats_location;
  storage_location_t _tree_registry_location; // tracked for a future gc(); not freed (matches packed)

  std::mutex _mutex; // guards _next_id
};

} // namespace points::converter
