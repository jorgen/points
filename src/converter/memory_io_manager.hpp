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

#include "io_manager.hpp"

#include <ankerl/unordered_dense.h>

#include <mutex>
#include <vector>

namespace points::converter
{

// An in-memory object store (like OpenVDS's in-memory IOManager). Backs ephemeral datasets (mem://)
// and, crucially, is the base for fault-injecting test subclasses: because read/write/info/remove are
// the io_manager_t virtuals, a subclass can override any of them to fail on a chosen object or the Nth
// call, short-read, or drop a specific write (e.g. the manifest) to exercise error handling and the
// write-ordering/crash-safety invariant deterministically, with no disk or process kill.
class memory_io_manager_t : public io_manager_t
{
public:
  memory_io_manager_t() = default;

  vio::task_t<points_error_t> read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read) override;
  vio::task_t<points_error_t> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override;
  vio::task_t<points_error_t> object_info(std::string name, object_info_t &out) override;
  vio::task_t<points_error_t> remove_object(std::string name) override;

protected:
  std::mutex _mutex;
  ankerl::unordered_dense::map<std::string, std::vector<uint8_t>> _objects;
};

} // namespace points::converter
