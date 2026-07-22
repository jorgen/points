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
#include "storage_backend.hpp"

#include "object_backend.hpp"
#include "packed_file_backend.hpp"
#include "url.hpp"

#include <vio/objstore/create_object_store.h>

namespace points::converter
{

std::unique_ptr<storage_backend_t> create_storage_backend(const std::string &url, vio::event_loop_t &event_loop, points_error_t &error)
{
  auto parsed = parse_url(url);

  // No scheme (a bare path) or file:// -> the single packed file backend.
  if (parsed.scheme.empty() || parsed.scheme == "file")
  {
    return std::make_unique<packed_file_backend_t>(parsed.path, event_loop, error);
  }

  // Object-per-blob over a vio object store (dir:// / mem:// / s3:// / az://), selected by the scheme.
  auto io = vio::objstore::create_io_manager(url, event_loop);
  if (!io.has_value())
  {
    error = {io.error().code != 0 ? io.error().code : -1, io.error().msg};
    return nullptr;
  }
  return std::make_unique<object_backend_t>(std::move(io.value()), event_loop);
}

} // namespace points::converter
