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
#include "morton_tree_coordinate_transform.hpp"
#include "error.hpp"

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

template<typename T1, size_t C1, typename T2, size_t C2>
typename std::enable_if<(sizeof(morton::morton_t<T1, C1>) > sizeof(morton::morton_t<T2, C2>))>::type downcast_point_buffer(const std::unique_ptr<uint8_t[]> &source, uint64_t source_size, std::unique_ptr<uint8_t[]> &target, uint64_t &target_size)
{
  uint64_t point_count = source_size / sizeof(morton::morton_t<T1, C1>);
  target_size = point_count * sizeof(morton::morton_t<T2, C2>);
  target.reset(new uint8_t[target_size]);
  const morton::morton_t<T1, C1> *source_morton = reinterpret_cast<const morton::morton_t<T1, C1> *>(source.get());
  morton::morton_t<T2, C2> *target_morton = reinterpret_cast<morton::morton_t<T2, C2> *>(target.get());
  for (uint64_t i = 0; i < point_count; i++)
  {
    morton::morton_downcast(source_morton[i], target_morton[i]);
  }
}
template<typename T1, size_t C1, typename T2, size_t C2>
typename std::enable_if<(sizeof(morton::morton_t<T1, C1>) <= sizeof(morton::morton_t<T2, C2>))>::type downcast_point_buffer(const std::unique_ptr<uint8_t[]> &source, uint64_t source_size, std::unique_ptr<uint8_t[]> &target, uint64_t &target_size)
{
  (void) source;
  (void) source_size;
  (void) target;
  (void) target_size;
}

template <typename T, typename MT, size_t C>
void convert_and_sort_morton(const tree_global_state_t &tree_state, points_t &points, double smallest_scale, type_t format, error_t &error)
{
  (void)error;
  auto &header = points.header;
  uint64_t count = header.public_header.point_count;
  uint64_t buffer_size = sizeof(morton::morton_t<MT,C>) * count;
  std::unique_ptr<uint8_t[]> world_morton_unique_ptr(new uint8_t[buffer_size]);
  morton::morton_t<MT,C> *morton_begin = reinterpret_cast<morton::morton_t<MT,C>*>(world_morton_unique_ptr.get());
  morton::morton_t<MT,C> *morton_end = morton_begin + count;
  uint64_t tmp[3];
  double pos[3];
  const vec_t<T> *point_data = reinterpret_cast<const vec_t<T>*>(points.buffers.buffers[0].data);
  double inv_scale = 1/smallest_scale;
  for (uint64_t i = 0; i < count; i++)
  {
    auto &point = point_data[i];
    pos[0] = point.data[0] * header.public_header.scale[0] + header.public_header.offset[0];
    pos[1] = point.data[1] * header.public_header.scale[1] + header.public_header.offset[1];
    pos[2] = point.data[2] * header.public_header.scale[2] + header.public_header.offset[2];
    tmp[0] = uint64_t((pos[0] - tree_state.offset[0]) * inv_scale);
    tmp[1] = uint64_t((pos[1] - tree_state.offset[1]) * inv_scale);
    tmp[2] = uint64_t((pos[2] - tree_state.offset[2]) * inv_scale);
    morton::encode(tmp, morton_begin[i]);
  }
  std::sort(morton_begin, morton_end);

  morton::morton192_t base_morton;
  morton::encode(tmp, base_morton);

  morton::morton_t<MT,C> first = *morton_begin;
  morton::morton_t<MT,C> last = *(morton_end - 1);
  assert(first < last);
  points.header.lod_span = morton::morton_lod(first, last);
  type_t new_type = morton_format_from_lod(points.header.lod_span);
  if (new_type != format)
  {
    std::unique_ptr<uint8_t[]> new_data;
    uint64_t new_buffer_size = 0;
    bool dont_move = false;
    if (new_type == type_m32 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton_t<uint32_t, 1>))
      downcast_point_buffer<MT,C, uint32_t, 1>(world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
    else if (new_type == type_m64 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton_t<uint64_t, 1>))
      downcast_point_buffer<MT,C, uint64_t, 1>(world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
    else if (new_type == type_m128 && sizeof(morton::morton_t<MT, C>) > sizeof(morton::morton_t<uint64_t, 2>))
      downcast_point_buffer<MT,C, uint64_t, 2>(world_morton_unique_ptr, buffer_size, new_data, new_buffer_size);
    else
      dont_move = true;
    if (!dont_move)
    {
      world_morton_unique_ptr = std::move(new_data);
      buffer_size = new_buffer_size;
      format = new_type;
    }
  }
  points.buffers.data[0] = std::move(world_morton_unique_ptr);
  points.buffers.buffers[0].data = points.buffers.data[0].get();
  points.buffers.buffers[0].size = buffer_size;
  points.header.point_format = format;
  morton::morton_upcast(first, base_morton, points.header.morton_min);
  morton::morton_upcast(last, base_morton, points.header.morton_max);
  assert(points.header.lod_span == morton::morton_lod(points.header.morton_min, points.header.morton_max));
}
template <typename T>
void convert_and_sort(const tree_global_state_t &tree_state, points_t &points, error_t &error)
{
  auto &header = points.header;

  double smallest_scale = std::min(std::min(std::min(header.public_header.scale[0], header.public_header.scale[1]), header.public_header.scale[2]), tree_state.scale);

  morton::morton192_t global_min = {};
  morton::morton192_t global_max;
  global_max.data[0] = ~uint64_t(0); global_max.data[1] = ~uint64_t(0); global_max.data[2] = ~uint64_t(0);
  double global_min_pos[3];
  double global_max_pos[3];
  convert_morton_to_pos(smallest_scale, tree_state.offset, global_min, global_min_pos);
  convert_morton_to_pos(smallest_scale, tree_state.offset, global_max, global_max_pos);

  type_t target_format;
  if (points.header.public_header.min[0] == -std::numeric_limits<double>::max() && points.header.public_header.min[1] == -std::numeric_limits<double>::max() && points.header.public_header.min[2] == -std::numeric_limits<double>::max()
    && points.header.public_header.max[0] == std::numeric_limits<double>::max() && points.header.public_header.min[1] == std::numeric_limits<double>::max() && points.header.public_header.min[2] == std::numeric_limits<double>::max())
  {
    target_format = type_t::type_m192;
  }
  else if (points.header.public_header.min[0] < global_min_pos[0] || points.header.public_header.min[1] < global_min_pos[1] || points.header.public_header.min[2] < global_min_pos[2]
    || global_max_pos[0] < points.header.public_header.max[0] || global_max_pos[1] < points.header.public_header.max[1] || global_max_pos[2] < points.header.public_header.max[2])
  {
    error.msg = "Data for is outside the dataset range.";
    error.code = -1;
    return;
  }
  else
  {
    convert_pos_to_morton(smallest_scale, tree_state.offset, points.header.public_header.min,  points.header.morton_min);
    convert_pos_to_morton(smallest_scale, tree_state.offset, points.header.public_header.max,  points.header.morton_max);
    int lod = morton::morton_lod(points.header.morton_min, points.header.morton_max);
    target_format = morton_format_from_lod(lod);
  }

  switch (target_format)
  {
    case type_m32:
      convert_and_sort_morton<T, uint32_t, 1>(tree_state, points, smallest_scale, target_format, error);
    break;
    case type_m64:
      convert_and_sort_morton<T, uint64_t, 1>(tree_state, points, smallest_scale, target_format, error);
    break;
    case type_m128:
      convert_and_sort_morton<T, uint64_t, 2>(tree_state, points, smallest_scale, target_format, error);
    break;
  case type_m192:
      convert_and_sort_morton<T, uint64_t, 3>(tree_state, points, smallest_scale, target_format, error);
    break;
  default:
    assert(false);
    break;
  }
}

void sort_points(const tree_global_state_t &tree_state, const std::vector<std::pair<type_t, components_t>> &attributes_def, points_t &points, error_t &error)
{
  switch (attributes_def.front().first)
  {
  case type_i32:
    convert_and_sort<int32_t>(tree_state, points, error);
    break;
  default:
    assert(false);
  }
}
}
} // namespace points
