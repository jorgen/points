/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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
#ifndef POINT_BUFFER_RENDER_HELPER_H
#define POINT_BUFFER_RENDER_HELPER_H

#include "buffer.hpp"
#include "conversion_types.hpp"
#include "storage_handler.hpp"
#include <glm_include.hpp>
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>
#include <points/render/buffer.h>

namespace points
{
namespace converter
{

struct dyn_points_draw_buffer_t
{
  render::draw_buffer_t render_list[3];
  render::buffer_t render_buffers[3];
  point_format_t format[2];
  std::unique_ptr<uint8_t[]> data[2];
  buffer_t data_info[2];
  int point_count;
  std::array<double, 3> offset;
  std::array<double, 3> scale;
  glm::mat4 camera_view;
};

template <typename MORTON_TYPE, typename DECODED_T>
void convert_points_to_vertex_data_morton(const tree_config_t &state, const read_only_points_t &read_points, buffer_t &vertex_data_info, std::array<double, 3> &output_offset, std::unique_ptr<uint8_t[]> &vertex_data)
{
  assert(read_points.data.size % sizeof(MORTON_TYPE) == 0);
  assert(read_points.header.point_count == read_points.data.size / sizeof(MORTON_TYPE));
  MORTON_TYPE *morton_array = static_cast<MORTON_TYPE *>(read_points.data.data);
  auto point_count = read_points.header.point_count;

  uint32_t buffer_size = uint32_t(point_count * sizeof(DECODED_T));
  vertex_data.reset(new uint8_t[buffer_size]);
  vertex_data_info = buffer_t(vertex_data.get(), buffer_size);
  auto vertex_data_ptr = vertex_data.get();
  std::array<float, 3> *decoded_array = reinterpret_cast<std::array<float, 3> *>(vertex_data_ptr);

  morton::morton192_t current;
  morton::morton192_t morton_min = read_points.header.morton_min;

  uint64_t min_int[3];
  morton::decode(morton_min, min_int);
  double min[3];
  min[0] = min_int[0] * state.scale;
  min[1] = min_int[1] * state.scale;
  min[2] = min_int[2] * state.scale;

  output_offset[0] = min[0];
  output_offset[1] = min[1];
  output_offset[2] = min[2];

  uint64_t tmp_pos[3];
  for (uint64_t i = 0; i < point_count; i++)
  {
    morton::morton_upcast(morton_array[i], morton_min, current);
    morton::decode(current, tmp_pos);
    for (int n = 0; n < 3; n++)
    {
      tmp_pos[n] -= min_int[n];
      decoded_array[i][n] = float(tmp_pos[n] * state.scale);
    }
  }
}

inline void convert_points_to_vertex_data(const tree_config_t &global_state, const read_only_points_t &read_points, dyn_points_draw_buffer_t &draw_buffer)
{
  auto pformat = read_points.header.point_format;
  switch (pformat.type)
  {
  case type_u8:
  case type_i8:
  case type_u16:
  case type_i16:
  case type_u32:
  case type_i32:
  case type_r32:
  case type_u64:
  case type_i64:
  case type_r64: {
    draw_buffer.data[0].reset(new uint8_t[read_points.data.size]);
    draw_buffer.data_info[0] = buffer_t(draw_buffer.data[0].get(), read_points.data.size);
    draw_buffer.format[0] = pformat;
    memcpy(draw_buffer.data[0].get(), read_points.data.data, read_points.data.size);
    break;
  }
  case type_m32:
    convert_points_to_vertex_data_morton<morton::morton32_t, std::array<uint16_t, 3>>(global_state, read_points, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m64:
    convert_points_to_vertex_data_morton<morton::morton64_t, std::array<uint32_t, 3>>(global_state, read_points, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m128:
    convert_points_to_vertex_data_morton<morton::morton128_t, std::array<uint64_t, 3>>(global_state, read_points, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m192:
    convert_points_to_vertex_data_morton<morton::morton192_t, std::array<uint64_t, 3>>(global_state, read_points, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  }
}
inline void convert_attribute_to_draw_buffer_data(const read_only_points_t &read_points, dyn_points_draw_buffer_t &draw_buffer, int dataslot)
{
  auto count = read_points.header.point_count;
  auto source_ptr = reinterpret_cast<std::array<uint16_t, 3> *>(read_points.data.data);
  assert(read_points.data.size == count * sizeof(std::array<uint16_t, 3>));

  auto target_size = uint32_t(count * sizeof(std::array<uint8_t, 3>));
  draw_buffer.data[dataslot].reset(new uint8_t[target_size]);
  draw_buffer.data_info[dataslot] = buffer_t(draw_buffer.data[dataslot].get(), target_size);
  draw_buffer.format[dataslot] = point_format_t(points::type_u8, components_3);
  auto target_ptr = reinterpret_cast<std::array<uint8_t, 3> *>(draw_buffer.data[dataslot].get());
  for (uint64_t i = 0; i < count; i++)
  {
    for (int n = 0; n < 3; n++)
    {
      target_ptr[i][n] = source_ptr[i][n] >> 8;
    }
  }
}
} // namespace converter
} // namespace points

#endif // POINT_BUFFER_RENDER_HELPER_H
