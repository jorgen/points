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
#include "native_node_data_loader.hpp"
#include "point_buffer_render_helper.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

namespace points::converter
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

  dyn_points_draw_buffer_t tmp;
  tmp.point_count = req.data_handler->header.point_count;
  tmp.data_handler = req.data_handler;
  convert_points_to_vertex_data(req.tree_config, *req.data_handler, tmp);
  convert_attribute_to_draw_buffer_data(*req.data_handler, tmp, 1);

  // Approach B runtime LOD: reorder the decoded vertex + its attribute coarse->fine so drawing the first
  // prefix_count[W+1] points is a screen-uniform subsample (see build_lod_order). Morton nodes only.
  std::array<uint32_t, 64> prefix_count = {};
  bool has_lod_order = false;
  {
    std::vector<uint32_t> perm;
    switch (req.data_handler->header.point_format.type)
    {
    case points_type_m32: build_lod_order<morton::morton32_t>(*req.data_handler, prefix_count, perm); has_lod_order = true; break;
    case points_type_m64: build_lod_order<morton::morton64_t>(*req.data_handler, prefix_count, perm); has_lod_order = true; break;
    case points_type_m128: build_lod_order<morton::morton128_t>(*req.data_handler, prefix_count, perm); has_lod_order = true; break;
    case points_type_m192: build_lod_order<morton::morton192_t>(*req.data_handler, prefix_count, perm); has_lod_order = true; break;
    default: break;
    }
    if (has_lod_order && tmp.point_count > 0)
    {
      const uint32_t n = tmp.point_count;
      const uint32_t vstride = tmp.data_info[0].size / n;
      auto reordered_vertex = reorder_points_by_perm(static_cast<const uint8_t *>(tmp.data_info[0].data), n, vstride, perm);
      tmp.data[0] = reordered_vertex;
      tmp.data_info[0] = points_converter_buffer_t(reordered_vertex.get(), n * vstride);
      if (tmp.data_info[1].data && tmp.data_info[1].size)
      {
        const uint32_t astride = tmp.data_info[1].size / n;
        auto reordered_attribute = reorder_points_by_perm(static_cast<const uint8_t *>(tmp.data_info[1].data), n, astride, perm);
        tmp.data[1] = reordered_attribute;
        tmp.data_info[1] = points_converter_buffer_t(reordered_attribute.get(), n * astride);
      }
    }
    else
    {
      has_lod_order = false;
    }
  }

  struct impl_data_t
  {
    std::shared_ptr<dyn_points_data_handler_t> data_handler;
    std::shared_ptr<uint8_t[]> vertex_data;
    std::shared_ptr<uint8_t[]> attribute_data;
  };

  auto impl = std::make_shared<impl_data_t>();
  impl->data_handler = std::move(req.data_handler);
  impl->vertex_data = std::move(tmp.data[0]);
  impl->attribute_data = std::move(tmp.data[1]);

  render::loaded_node_data_t result;
  result.vertex_data = tmp.data_info[0].data;
  result.vertex_data_size = tmp.data_info[0].size;
  result.vertex_type = tmp.format[0].type;
  result.vertex_components = tmp.format[0].components;

  result.attribute_data = tmp.data_info[1].data;
  result.attribute_data_size = tmp.data_info[1].size;
  result.attribute_type = tmp.format[1].type;
  result.attribute_components = tmp.format[1].components;

  result.point_count = tmp.point_count;
  result.offset = tmp.offset;
  result.draw_type = tmp.draw_type;
  result.prefix_count = prefix_count;
  result.has_lod_order = has_lod_order;

  result._impl_data = std::move(impl);

  return result;
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

} // namespace points::converter
