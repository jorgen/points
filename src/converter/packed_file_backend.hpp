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

#include "blob_manager.hpp"
#include "index_format.hpp"
#include "storage_backend.hpp"

#include <vio/operation/file.h>

#include <mutex>
#include <optional>
#include <string>

namespace points::converter
{

// The single-file (packed) storage backend: all blobs packed into one file's linear byte space by
// free_blob_manager_t, with the 128-byte index/superblock at offset 0 rewritten in place and fsync'd
// on every checkpoint. This is the historical behavior of storage_handler, extracted verbatim.
class packed_file_backend_t : public storage_backend_t
{
public:
  packed_file_backend_t(std::string file_name, vio::event_loop_t &event_loop, points_error_t &error);
  ~packed_file_backend_t() override;

  [[nodiscard]] bool exists() const override;
  [[nodiscard]] points_error_t open_for_write(bool truncate) override;
  [[nodiscard]] points_error_t read_index(index_load_t &out) override;
  [[nodiscard]] points_error_t restore_allocator(const std::unique_ptr<uint8_t[]> &data, uint32_t size) override;
  void allocate_blob(uint32_t size, storage_location_t &out) override;
  vio::task_t<points_error_t> write_allocated(storage_location_t location, std::shared_ptr<uint8_t[]> data) override;
  vio::task_t<points_error_t> read_blob(storage_location_t location, uint8_t *dst, uint32_t &bytes_read) override;
  vio::task_t<points_error_t> write_index(checkpoint_t checkpoint) override;

private:
  std::string _file_name;
  vio::event_loop_t &_event_loop;
  std::optional<vio::auto_close_file_t> _file;
  bool _file_exists = false;
  uint32_t _serialized_index_size = k_serialized_index_size;
  free_blob_manager_t _blob_manager;

  // Metadata blob locations from the previously committed index, freed at the next checkpoint.
  // NB: matching the original behavior, read_index records only stats/perf here; attributes/free-blobs
  // stay {0} after a reopen (they are only tracked once written by this session's first checkpoint).
  storage_location_t _attributes_location;
  storage_location_t _blobs_location;
  storage_location_t _stats_location;
  storage_location_t _perf_stats_location;

  std::mutex _mutex; // guards _blob_manager during allocate_blob
};

} // namespace points::converter
