/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#include "native_node_data_loader.hpp"
#include "point_buffer_render_helper.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

namespace dew::converter
{

native_node_data_loader_t::native_node_data_loader_t(storage_handler_t &storage_handler)
  : _storage_handler(storage_handler)
{
}

render::load_handle_t native_node_data_loader_t::request_load(const void *request_data, uint32_t request_size)
{
  assert(request_size == sizeof(native_load_request_t));
  (void)request_size;

  native_load_request_t req;
  std::memcpy(&req, request_data, sizeof(req));

  auto data_handler = std::make_shared<dyn_points_data_handler_t>(req.format);
  data_handler->start_requests(data_handler, _storage_handler, req.locations);

  auto handle = _next_handle.fetch_add(1);

  std::lock_guard<std::mutex> lock(_mutex);
  _pending[handle] = {std::move(data_handler), req.tree_config};

  return handle;
}

bool native_node_data_loader_t::is_ready(render::load_handle_t handle)
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = _pending.find(handle);
  if (it == _pending.end())
    return false;
  return it->second.data_handler->is_done();
}

render::loaded_node_data_t native_node_data_loader_t::get_data(render::load_handle_t handle)
{
  pending_request_t req;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _pending.find(handle);
    assert(it != _pending.end());
    req = std::move(it->second);
    _pending.erase(it);
  }

  // All the CPU decode work now lives in the standalone, storage-free decode_node() seam (node_decode.hpp),
  // which a decode Web Worker can also call. The data_handler is passed as the salvage handler so a promoted
  // spanning leaf can still recover its pre-reorder morton codes.
  return decode_node(req.data_handler->as_decode_input(), req.tree_config, req.data_handler);
}

void native_node_data_loader_t::cancel(render::load_handle_t handle)
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = _pending.find(handle);
  if (it != _pending.end())
  {
    it->second.data_handler->cancel_requests();
    _pending.erase(it);
  }
}

} // namespace dew::converter
