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
#include "io_manager.hpp"

#include "file_dir_io_manager.hpp"
#include "memory_io_manager.hpp"

namespace points::converter
{

std::unique_ptr<io_manager_t> create_io_manager(const std::string &scheme, const std::string &path, vio::event_loop_t &event_loop, points_error_t &error)
{
  if (scheme == "dir")
    return std::make_unique<file_dir_io_manager_t>(path, event_loop);
  if (scheme == "mem")
    return std::make_unique<memory_io_manager_t>();

  // s3://, az://, https:// are designed for but not yet implemented (next round).
  error.code = -1;
  error.msg = "Unsupported io_manager scheme: '" + scheme + "'";
  return nullptr;
}

} // namespace points::converter
