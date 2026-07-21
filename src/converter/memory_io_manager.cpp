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
#include "memory_io_manager.hpp"

#include <cstring>

namespace points::converter
{

vio::task_t<points_error_t> memory_io_manager_t::read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _objects.find(name);
  if (it == _objects.end())
  {
    points_error_t error;
    error.code = 1;
    error.msg = "Object not found: " + name;
    co_return error;
  }
  const auto &bytes = it->second;
  uint64_t offset = range.offset < 0 ? 0 : uint64_t(range.offset);
  uint64_t size = range.size < 0 ? (bytes.size() > offset ? bytes.size() - offset : 0) : uint64_t(range.size);
  if (offset + size > bytes.size())
  {
    points_error_t error;
    error.code = 1;
    error.msg = "Read out of range for object: " + name;
    co_return error;
  }
  if (size > 0)
    memcpy(dst, bytes.data() + offset, size);
  bytes_read = uint32_t(size);
  co_return points_error_t{};
}

vio::task_t<points_error_t> memory_io_manager_t::write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size)
{
  std::unique_lock<std::mutex> lock(_mutex);
  std::vector<uint8_t> bytes(size);
  if (size > 0)
    memcpy(bytes.data(), data.get(), size);
  _objects[std::move(name)] = std::move(bytes);
  co_return points_error_t{};
}

vio::task_t<points_error_t> memory_io_manager_t::object_info(std::string name, object_info_t &out)
{
  std::unique_lock<std::mutex> lock(_mutex);
  auto it = _objects.find(name);
  out.exists = it != _objects.end();
  out.size = out.exists ? it->second.size() : 0;
  co_return points_error_t{};
}

vio::task_t<points_error_t> memory_io_manager_t::remove_object(std::string name)
{
  std::unique_lock<std::mutex> lock(_mutex);
  _objects.erase(name);
  co_return points_error_t{};
}

} // namespace points::converter
