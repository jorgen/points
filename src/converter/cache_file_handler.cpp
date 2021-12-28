/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#include "cache_file_handler.hpp"

#include <uv.h>

#include <fmt/printf.h>

#include <assert.h>
#include <fcntl.h>
namespace points
{
namespace converter
{
cache_file_handler_t::cache_file_handler_t(const std::string &cache_file, event_pipe_t<error_t> &cache_file_error)
  : _cache_file_name(cache_file)
  , _file_handle(0)
  , _file_opened(false)
  , _cache_file_error(cache_file_error)
{
  _open_request.data = this;
  _event_loop.add_about_to_block_listener(this);
#ifdef WIN32
 int open_mode =  _S_IREAD | _S_IWRITE;
#else
  int open_mode = 0666;
#endif
  uv_fs_open(_event_loop.loop(), &_open_request, _cache_file_name.c_str(), UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC, open_mode, [](uv_fs_t *request)
  { 
    cache_file_handler_t &self = *static_cast<cache_file_handler_t *>(request->data);
    self.handle_open_cache_file(request);
  });
}

void cache_file_handler_t::about_to_block()
{
}

void cache_file_handler_t::handle_open_cache_file(uv_fs_t *request)
{
  _file_handle = uv_file(request->result);
  _file_opened = _file_handle > 0;
  if (_file_handle < 0)
  {
    error_t error;
    error.code = (int)_file_handle;
    error.msg = uv_strerror(_file_handle);
    _cache_file_error.post_event(error);
  }
}

}
} // namespace points