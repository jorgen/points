/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
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

#include "decode.hpp"

#include "format_util.hpp"
#include "morton_tree_coordinate_transform.hpp"

#include <cstring>

namespace dew::access
{
namespace
{

// Restore the node's cell origin. Stored codes keep only the bits below the node's lod_span, so the
// masked-off high bits -- the cell the node occupies -- have to be added back.
void node_origin(const morton::morton192_t &header_min, int lod_span, const tree_config_t &config, uint64_t (&origin_grid)[3], double origin_world[3])
{
  const auto mask = morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(lod_span));
  const morton::morton192_t masked = morton::morton_and(header_min, mask);
  morton::decode(masked, origin_grid);
  for (int i = 0; i < 3; i++)
    origin_world[i] = double(origin_grid[i]) * config.scale + config.offset[i];
}

template <typename MORTON_T>
void decode_one(const void *src, uint32_t index, const morton::morton192_t &cell_min, uint64_t (&grid)[3])
{
  const auto *codes = static_cast<const MORTON_T *>(src);
  morton::morton192_t world;
  morton::morton_upcast(codes[index], cell_min, world);
  morton::decode(world, grid);
}

} // namespace

bool decode_positions(const void *morton_data, uint32_t data_size, uint32_t point_count, point_format_t format, const morton::morton192_t &header_min, int lod_span, const tree_config_t &tree_config,
                      position_format_t out_format, void *dst, uint64_t dst_size, double origin_out[3])
{
  if (dst_size < uint64_t(point_count) * position_stride(out_format))
    return false;
  const uint32_t stride = uint32_t(size_for_format(format.type, format.components));
  if (stride == 0 || uint64_t(point_count) * stride > data_size)
    return false;

  uint64_t origin_grid[3];
  node_origin(header_min, lod_span, tree_config, origin_grid, origin_out);
  const auto cell_min = morton::morton_and(header_min, morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(lod_span)));

  auto *out64 = static_cast<double *>(dst);
  auto *out32 = static_cast<float *>(dst);
  auto *outi = static_cast<int32_t *>(dst);

  for (uint32_t i = 0; i < point_count; i++)
  {
    uint64_t grid[3];
    switch (format.type)
    {
    case dew_type_m32:
      decode_one<morton::morton32_t>(morton_data, i, cell_min, grid);
      break;
    case dew_type_m64:
      decode_one<morton::morton64_t>(morton_data, i, cell_min, grid);
      break;
    case dew_type_m128:
      decode_one<morton::morton128_t>(morton_data, i, cell_min, grid);
      break;
    case dew_type_m192:
      morton::decode(static_cast<const morton::morton192_t *>(morton_data)[i], grid);
      break;
    default:
      return false; // positions are always morton-coded in a .dew node
    }

    switch (out_format)
    {
    case position_format_t::r64_absolute:
      for (int c = 0; c < 3; c++)
        out64[i * 3 + c] = double(grid[c]) * tree_config.scale + tree_config.offset[c];
      break;
    case position_format_t::r32_relative:
      for (int c = 0; c < 3; c++)
        out32[i * 3 + c] = float(double(grid[c] - origin_grid[c]) * tree_config.scale);
      break;
    case position_format_t::i32_grid:
      for (int c = 0; c < 3; c++)
        outi[i * 3 + c] = int32_t(int64_t(grid[c]) - int64_t(origin_grid[c]));
      break;
    }
  }
  return true;
}

uint32_t clip_to_box(void *positions, position_format_t format, const double origin[3], double scale, uint32_t point_count, const double box_min[3], const double box_max[3], attribute_span_t *attributes, uint32_t attribute_count)
{
  const uint32_t stride = position_stride(format);
  auto *bytes = static_cast<uint8_t *>(positions);

  auto world_at = [&](uint32_t i, double out[3]) {
    switch (format)
    {
    case position_format_t::r64_absolute:
    {
      const auto *p = reinterpret_cast<const double *>(bytes + uint64_t(i) * stride);
      out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
      break;
    }
    case position_format_t::r32_relative:
    {
      const auto *p = reinterpret_cast<const float *>(bytes + uint64_t(i) * stride);
      for (int c = 0; c < 3; c++)
        out[c] = origin[c] + double(p[c]);
      break;
    }
    case position_format_t::i32_grid:
    {
      // i32 values are GRID STEPS relative to the node origin, while the caller's box is in world
      // units -- so they have to be scaled before the comparison, not just offset.
      const auto *p = reinterpret_cast<const int32_t *>(bytes + uint64_t(i) * stride);
      for (int c = 0; c < 3; c++)
        out[c] = origin[c] + double(p[c]) * scale;
      break;
    }
    }
  };

  uint32_t kept = 0;
  for (uint32_t i = 0; i < point_count; i++)
  {
    double p[3];
    world_at(i, p);
    if (p[0] < box_min[0] || p[0] > box_max[0] || p[1] < box_min[1] || p[1] > box_max[1] || p[2] < box_min[2] || p[2] > box_max[2])
      continue;
    if (kept != i)
    {
      memcpy(bytes + uint64_t(kept) * stride, bytes + uint64_t(i) * stride, stride);
      for (uint32_t a = 0; a < attribute_count; a++)
      {
        auto &span = attributes[a];
        if (!span.data || span.stride == 0)
          continue;
        memcpy(span.data + uint64_t(kept) * span.stride, span.data + uint64_t(i) * span.stride, span.stride);
      }
    }
    kept++;
  }
  return kept;
}

} // namespace dew::access
