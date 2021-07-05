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
#include "sorter_p.h"
#include "input_header_p.h"
#include "morton_p.h"
#include "memcpy_array_p.h"

#include <points/converter/default_attribute_names.h>
#include <vector>

#include <fmt/printf.h>

#include <assert.h>
namespace points
{
namespace converter
{

template<typename T>
struct morton_sort_t 
{
  T data[3];
  bool operator<(const morton_sort_t& other) const
  {
    if (data[2] == other.data[2])
    {
      if (data[1] == other.data[1])
      {
        return data[0] < other.data[0];
      }
      return data[1] < other.data[1];
    }
    return data[2] < other.data[2];
  }
};
template <typename T>
void convert_and_sort(buffer_t &buffer, internal_header_t &header)
{
  using uT = typename std::make_unsigned<T>::type;
  T *data = (T *)buffer.data;
  T *end = data + (header.point_count * 3);
  if (std::isnan(header.min[0]) || std::isnan(header.min[1]) || std::isnan(header.min[2]))
  {
    header.min[0] = std::numeric_limits<double>::max();
    header.min[1] = std::numeric_limits<double>::max();
    header.min[2] = std::numeric_limits<double>::max();
    double offset[3];
    offset[0] = std::isnan(header.offset[0]) ? 0.0 : header.offset[0];
    offset[1] = std::isnan(header.offset[1]) ? 0.0 : header.offset[1];
    offset[2] = std::isnan(header.offset[2]) ? 0.0 : header.offset[2];
    for (T *p = data; p < end; p += 3)
    {
      double new_min[3];
      new_min[0] = p[0] * header.scale[0] + offset[0];
      new_min[1] = p[1] * header.scale[1] + offset[1];
      new_min[2] = p[2] * header.scale[2] + offset[2];
      if (new_min[0] < header.min[0])
        header.min[0] = new_min[0];
      if (new_min[1] < header.min[1])
        header.min[1] = new_min[1];
      if (new_min[2] < header.min[2])
        header.min[2] = new_min[2];
    }
    header_p_calculate_morton_aabb(header);
  }

  double inv_scale[3];
  inv_scale[0] = 1 / header.scale[0];
  inv_scale[1] = 1 / header.scale[1];
  inv_scale[2] = 1 / header.scale[2];

  if (-header.min[0] != header.offset[0] || -header.min[1] != header.offset[1] || -header.min[2] != header.offset[2])
  {
    double offset[3];
    offset[0] = std::isnan(header.offset[0]) ? 0.0 : header.offset[0];
    offset[1] = std::isnan(header.offset[1]) ? 0.0 : header.offset[1];
    offset[2] = std::isnan(header.offset[2]) ? 0.0 : header.offset[2];


    double new_offset[3];
    new_offset[0] = -header.min[0] - (1 * header.scale[0]); 
    new_offset[1] = -header.min[1] - (1 * header.scale[1]); 
    new_offset[2] = -header.min[2] - (1 * header.scale[2]); 

    for (T *p = data; p < end; p += 3)
    {
      p[0] = T(((p[0] * header.scale[0] - offset[0]) + new_offset[0]) * inv_scale[0]);
      p[1] = T(((p[1] * header.scale[1] - offset[1]) + new_offset[1]) * inv_scale[1]);
      p[2] = T(((p[2] * header.scale[2] - offset[2]) + new_offset[2]) * inv_scale[2]);

      morton::encode((uT *)p);
    }
    memcpy_array(header.offset, new_offset);
  }
  else
  {
    for (T *p = data; p < end; p += 3)
    {
      morton::encode((uT *)p);
    }
  }
  //  uint64_t mask[3];
//  morton::morton_xor(header.morton_max, header.morton_min, mask);
//  int msb = morton::morton_msb(mask);
//  morton::morton_mask_create(msb, mask);
//  morton::morton_mask_invert(mask);
//
//  uint64_t new_morton_min[3];
//  static_assert(sizeof(header.morton_min) == sizeof(new_morton_min), "size of morton_min not 64*3 bits");
//  memcpy_array(new_morton_min, header.morton_min);
//  morton::morton_mask(mask, new_morton_min);
//
//  double new_offset[3];
//  morton::decode(new_morton_min, header.scale, new_offset);
//

  morton_sort_t<uT> *morton_begin = (morton_sort_t<uT> *)data;
  morton_sort_t<uT> *morton_end = (morton_sort_t<uT> *)end;
  std::sort(morton_begin, morton_end);

  uT final_min[3];
  memcpy(final_min, morton_begin, sizeof(final_min));
  uT final_max[3];
  memcpy(final_max, morton_end - 1, sizeof(final_max));
  morton::decode(final_min);
  morton::decode(final_max);
  header.min[0] = final_min[0] * header.scale[0] - header.offset[0];
  header.min[1] = final_min[1] * header.scale[1] - header.offset[1];
  header.min[2] = final_min[2] * header.scale[2] - header.offset[2];
  
  header.max[0] = final_max[0] * header.scale[0] - header.offset[0];
  header.max[1] = final_max[1] * header.scale[1] - header.offset[1];
  header.max[2] = final_max[2] * header.scale[2] - header.offset[2];

  header_p_calculate_morton_aabb(header);
}

void sort_points(points_t &points)
{
  assert(points.header.attributes.attributes[0].name_size == strlen(POINTS_ATTRIBUTE_XYZ));
  assert(memcmp(points.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ, points.header.attributes.attributes[0].name_size) == 0);

  uint64_t expected_points_buffer_size = size_for_format(points.header.attributes.attributes[0].format) * 3 * points.header.point_count;
  assert(points.buffers.buffers[0].size == expected_points_buffer_size);

  switch (points.header.attributes.attributes[0].format)
  {
  case format_i32:
    convert_and_sort<int32_t>(points.buffers.buffers[0], points.header);
    break;
  default:
    assert(false);
  }
}
}
} // namespace points
