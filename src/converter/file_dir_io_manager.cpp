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
#include "file_dir_io_manager.hpp"

#include <vio/operation/file.h>

#include <uv.h>

#include <optional>

#ifndef WIN32
#include <sys/stat.h>
#endif

namespace points::converter
{

static points_error_t io_error(int code, std::string msg)
{
  points_error_t e;
  e.code = code;
  e.msg = std::move(msg);
  return e;
}

file_dir_io_manager_t::file_dir_io_manager_t(std::string dir, vio::event_loop_t &event_loop)
  : _dir(std::move(dir))
  , _event_loop(event_loop)
{
}

vio::task_t<points_error_t> file_dir_io_manager_t::read_object(std::string name, uint8_t *dst, io_range_t range, uint32_t &bytes_read)
{
  std::string path = _dir + "/" + name;
  auto open_result = vio::open_file(_event_loop, path, vio::file_open_flags_t(vio::file_open_flag_t::rdonly), 0);
  if (!open_result.has_value())
    co_return io_error(1, "Object not found: " + name);
  auto &file = open_result.value();

  uint64_t offset = range.offset < 0 ? 0 : uint64_t(range.offset);
  uint64_t size;
  if (range.size < 0)
  {
    auto st = vio::stat_file(_event_loop, path);
    if (!st.has_value())
      co_return io_error(1, "Could not stat object: " + name);
    uint64_t fsize = uint64_t(st.value().st_size);
    size = fsize > offset ? fsize - offset : 0;
  }
  else
  {
    size = uint64_t(range.size);
  }

  if (size == 0)
  {
    bytes_read = 0;
    co_return points_error_t{};
  }

  auto r = co_await vio::read_file(_event_loop, *file, dst, size, int64_t(offset));
  if (!r.has_value())
    co_return io_error(r.error().code, r.error().msg);
  bytes_read = uint32_t(r.value());
  co_return points_error_t{};
}

vio::task_t<points_error_t> file_dir_io_manager_t::write_object(std::string name, std::shared_ptr<uint8_t[]> data, uint64_t size)
{
  // Ensure the directory exists (idempotent; only ever reached on a write path).
#ifdef WIN32
  int dir_mode = 0;
#else
  int dir_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
#endif
  (void)vio::mkdir_path(_event_loop, _dir, dir_mode);

  std::string tmpl = _dir + "/.tmp_XXXXXX";
  auto tmp = vio::mkstemp_file(_event_loop, tmpl);
  if (!tmp.has_value())
    co_return io_error(tmp.error().code, tmp.error().msg);

  std::optional<vio::auto_close_file_t> tmp_file(std::move(tmp.value().first));
  std::string tmp_path = std::move(tmp.value().second);

  if (size > 0)
  {
    auto wr = co_await vio::write_file(_event_loop, **tmp_file, data.get(), size, 0);
    if (!wr.has_value())
    {
      tmp_file.reset();
      (void)vio::unlink_file(_event_loop, tmp_path);
      co_return io_error(wr.error().code, wr.error().msg);
    }
  }

  // Make the data durable before it becomes visible under the final name.
  uv_fs_t fsync_req = {};
  uv_fs_fsync(_event_loop.loop(), &fsync_req, (**tmp_file).handle, NULL);
  uv_fs_req_cleanup(&fsync_req);

  tmp_file.reset(); // close before rename (required on Windows)

  auto rn = vio::rename_file(_event_loop, tmp_path, _dir + "/" + name);
  if (!rn.has_value())
  {
    (void)vio::unlink_file(_event_loop, tmp_path);
    co_return io_error(rn.error().code, rn.error().msg);
  }
  co_return points_error_t{};
}

vio::task_t<points_error_t> file_dir_io_manager_t::object_info(std::string name, object_info_t &out)
{
  auto st = vio::stat_file(_event_loop, _dir + "/" + name);
  out.exists = st.has_value();
  out.size = st.has_value() ? uint64_t(st.value().st_size) : 0;
  co_return points_error_t{};
}

vio::task_t<points_error_t> file_dir_io_manager_t::remove_object(std::string name)
{
  // Idempotent: removing a missing object is not an error.
  (void)vio::unlink_file(_event_loop, _dir + "/" + name);
  co_return points_error_t{};
}

} // namespace points::converter
