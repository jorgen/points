/************************************************************************
** dewfall - point cloud management software.
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
#include "node_decode.hpp"

#include "morton.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace dew::converter
{

render::loaded_node_data_t decode_node(const decode_input_t &in, const tree_config_t &tree_config, std::shared_ptr<dyn_points_data_handler_t> salvage_handler)
{
  dyn_points_draw_buffer_t tmp;
  tmp.point_count = in.header.point_count;

  convert_points_to_vertex_data(tree_config, in, tmp);
  convert_attribute_to_draw_buffer_data(in, tmp, 1);

  // Approach B runtime LOD: reorder the decoded vertex + its attribute coarse->fine so drawing the first
  // prefix_count[W+1] points is a screen-uniform subsample (see build_lod_order_from_mortons). Morton nodes
  // only. The reordered per-point rep_level rides along as a u8 buffer for the per-point LOD test in the shader.
  std::array<uint32_t, 64> prefix_count = {};
  bool has_lod_order = false;
  std::shared_ptr<uint8_t[]> rep_level_buffer;
  uint32_t rep_level_size = 0;
  {
    std::vector<uint32_t> perm;
    std::vector<uint8_t> rep_level;
    const auto *m = in.data_info[0].data;
    const uint32_t n0 = in.header.point_count;
    switch (in.header.point_format.type)
    {
    case dew_type_m32: build_lod_order_from_mortons<morton::morton32_t>(static_cast<const morton::morton32_t *>(m), n0, prefix_count, perm, rep_level); has_lod_order = true; break;
    case dew_type_m64: build_lod_order_from_mortons<morton::morton64_t>(static_cast<const morton::morton64_t *>(m), n0, prefix_count, perm, rep_level); has_lod_order = true; break;
    case dew_type_m128: build_lod_order_from_mortons<morton::morton128_t>(static_cast<const morton::morton128_t *>(m), n0, prefix_count, perm, rep_level); has_lod_order = true; break;
    case dew_type_m192: build_lod_order_from_mortons<morton::morton192_t>(static_cast<const morton::morton192_t *>(m), n0, prefix_count, perm, rep_level); has_lod_order = true; break;
    default: break;
    }
    if (has_lod_order && tmp.point_count > 0)
    {
      const uint32_t n = tmp.point_count;
      // The decoded vertex is always packed r32x3 (12 bytes/point); the reordered buffer is exactly 12*n bytes.
      const uint32_t vstride = 3u * uint32_t(sizeof(float));
      auto reordered_vertex = reorder_points_by_perm(static_cast<const uint8_t *>(tmp.data_info[0].data), n, vstride, perm);
      tmp.data[0] = reordered_vertex;
      tmp.data_info[0] = dew_converter_buffer_t(reordered_vertex.get(), n * vstride);
      if (tmp.data_info[1].data && tmp.data_info[1].size)
      {
        const uint32_t astride = tmp.data_info[1].size / n;
        auto reordered_attribute = reorder_points_by_perm(static_cast<const uint8_t *>(tmp.data_info[1].data), n, astride, perm);
        tmp.data[1] = reordered_attribute;
        tmp.data_info[1] = dew_converter_buffer_t(reordered_attribute.get(), n * astride);
      }
      // rep_level is already in perm (coarse->fine) order; copy into an owned buffer.
      rep_level_buffer = std::make_shared<uint8_t[]>(n);
      std::memcpy(rep_level_buffer.get(), rep_level.data(), n);
      rep_level_size = n;
    }
    else
    {
      has_lod_order = false;
    }
    // Non-morton (or empty-order) nodes get a sentinel rep_level of 255 so the shader keeps every point.
    if (!rep_level_buffer && tmp.point_count > 0)
    {
      rep_level_buffer = std::make_shared<uint8_t[]>(tmp.point_count);
      std::memset(rep_level_buffer.get(), 255, tmp.point_count);
      rep_level_size = tmp.point_count;
    }
  }

  auto impl = std::make_shared<loaded_node_impl_data_t>();
  impl->data_handler = std::move(salvage_handler);
  impl->vertex_data = std::move(tmp.data[0]);
  impl->attribute_data = std::move(tmp.data[1]);
  impl->rep_level_data = rep_level_buffer;

  render::loaded_node_data_t result;
  result.vertex_data = tmp.data_info[0].data;
  result.vertex_data_size = tmp.data_info[0].size;
  result.vertex_type = tmp.format[0].type;
  result.vertex_components = tmp.format[0].components;

  result.attribute_data = tmp.data_info[1].data;
  result.attribute_data_size = tmp.data_info[1].size;
  result.attribute_type = tmp.format[1].type;
  result.attribute_components = tmp.format[1].components;

  result.rep_level_data = rep_level_buffer ? rep_level_buffer.get() : nullptr;
  result.rep_level_data_size = rep_level_size;

  result.point_count = tmp.point_count;
  result.offset = tmp.offset;
  result.draw_type = tmp.draw_type;
  result.prefix_count = prefix_count;
  result.has_lod_order = has_lod_order;

  result._impl_data = std::move(impl);

  return result;
}

} // namespace dew::converter
