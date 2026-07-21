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

#include "error.hpp"

#include <vio/event_loop.h>
#include <vio/task.h>

#include <cstdint>
#include <memory>
#include <string>

namespace points::converter
{

// A byte range within an object. size < 0 means "to the end of the object" (i.e. the whole object
// when offset is 0). Modeled on OpenVDS IOManager's IORange, but coroutine-native.
struct io_range_t
{
  int64_t offset = 0;
  int64_t size = -1;
};

struct object_info_t
{
  bool exists = false;
  uint64_t size = 0;
};

// An object-store transport, OpenVDS-IOManager style but built on vio coroutines. Whole-object
// (immutable) writes, range-capable reads. Backends: local directory-of-files, in-memory (testing),
// and later S3/Azure/http. All methods run on the storage handler's event loop; errors are
// points_error_t (code 0 == success). Object names are opaque keys chosen by the storage backend.
class io_manager_t
{
public:
  virtual ~io_manager_t() = default;

  // Read (a range of) an object into dst (caller-owned, large enough for the range). bytes_read is
  // set to the number of bytes actually read. A missing object is an error.
  virtual vio::task_t<points_error_t> read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read) = 0;

  // Write a whole object (create or atomically replace). `size` bytes of `data`.
  virtual vio::task_t<points_error_t> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) = 0;

  virtual vio::task_t<points_error_t> object_info(std::string name, object_info_t &out) = 0;

  // Remove an object. Removing a missing object is not an error (idempotent).
  virtual vio::task_t<points_error_t> remove_object(std::string name) = 0;
};

// Build an io_manager for a scheme ("dir", "mem", later "s3"/"az"/"https"). `path` is the URL body
// after "scheme://" (e.g. a directory path, or a bucket/prefix). Returns nullptr + error on an
// unsupported scheme.
std::unique_ptr<io_manager_t> create_io_manager(const std::string &scheme, const std::string &path, vio::event_loop_t &event_loop, points_error_t &error);

} // namespace points::converter
