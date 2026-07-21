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

#include <string>

namespace points::converter
{

// One file per object under a directory: object "name" -> "<dir>/<name>". Writes are atomic
// (write a unique temp file, fsync, rename over the final name) so a torn/partial object is never
// visible; reads are ranged file reads. This is the first object-per-blob backend and the model
// the S3/Azure backends will follow.
class file_dir_io_manager_t : public io_manager_t
{
public:
  file_dir_io_manager_t(std::string dir, vio::event_loop_t &event_loop);

  vio::task_t<points_error_t> read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read) override;
  vio::task_t<points_error_t> write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size) override;
  vio::task_t<points_error_t> object_info(std::string name, object_info_t &out) override;
  vio::task_t<points_error_t> remove_object(std::string name) override;

private:
  std::string _dir;
  vio::event_loop_t &_event_loop;
};

} // namespace points::converter
