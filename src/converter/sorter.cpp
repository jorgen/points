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
#include "sorter.hpp"
#include "input_header.hpp"
#include "morton.hpp"
#include "memcpy_array.hpp"

#include <points/converter/default_attribute_names.h>
#include <vector>

#include <fmt/printf.h>

#include <assert.h>
namespace points
{
namespace converter
{

template<typename T>
struct vec_t
{
  T data[3];
};

template <typename T>
void convert_and_sort(const tree_global_state_t &tree_state, points_t &points)
{
  auto &header = points.header;

  double smallest_scale = std::min(std::min(std::min(header.scale[0], header.scale[1]), header.scale[2]), tree_state.scale);



  uint64_t count = header.point_count;
  uint64_t buffer_size = sizeof(morton::morton192_t) * count;
  std::unique_ptr<uint8_t[]> world_morton_unique_ptr(new uint8_t[buffer_size]);
  morton::morton192_t *morton_begin = reinterpret_cast<morton::morton192_t *>(world_morton_unique_ptr.get());
  morton::morton192_t *morton_end = morton_begin + count;
  uint64_t tmp[3];
  double pos[3];
  const vec_t<T> *point_data = reinterpret_cast<const vec_t<T>*>(points.buffers.buffers[0].data);
  double inv_scale = 1/smallest_scale;
  for (uint64_t i = 0; i < count; i++)
  {
    auto &point = point_data[i];
    pos[0] = point.data[0] * header.scale[0] + header.offset[0];
    pos[1] = point.data[1] * header.scale[1] + header.offset[1];
    pos[2] = point.data[2] * header.scale[2] + header.offset[2];
    tmp[0] = pos[0] - tree_state.offset[0] * inv_scale;
    tmp[1] = pos[1] - tree_state.offset[1] * inv_scale;
    tmp[2] = pos[2] - tree_state.offset[2] * inv_scale;
    morton::encode(tmp, morton_begin[i]);
  }
  std::sort(morton_begin, morton_end);

  morton::morton192_t first = *morton_begin;
  morton::morton192_t last = *(morton_end - 1);
  assert(first < last);
  points.buffers.data[0] = std::move(world_morton_unique_ptr);
  points.buffers.buffers[0].data = points.buffers.data[0].get();
  points.buffers.buffers[0].size = buffer_size;
  points.header.point_format = format_t::format_m192;
  points.header.morton_min = first;
  points.header.morton_max = last;
  points.header.lod_span = morton::morton_lod(first, last);

  //int msb = morton::morton_lod(first, last) * 3 + 3;
  //points.buffers.buffers[0]
  //if (msb < 32)
  //{

  //}
  //else if (msb < 64)
  //{

  //}
  //else if (msb < 128)
  //{

  //}
  //else
  //{

  //}
}

void sort_points(const tree_global_state_t &tree_state, const std::vector<std::pair<format_t, components_t>> &attributes_def, points_t &points)
{
  switch (attributes_def.front().first)
  {
  case format_i32:
    convert_and_sort<int32_t>(tree_state, points);
    break;
  default:
    assert(false);
  }
}
}
} // namespace points
