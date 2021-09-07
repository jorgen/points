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
void convert_and_sort(const tree_global_state_t &tree_state, buffer_t &buffer, internal_header_t &header)
{
  using uT = typename std::make_unsigned<T>::type;
  T *begin = (T *)buffer.data;
  T *end = begin + (header.point_count * 3);
  for (T *p = begin; p < end; p += 3)
  {
    morton::encode((uT *)p);
  }

  morton_sort_t<uT> *morton_begin = (morton_sort_t<uT> *)begin;
  morton_sort_t<uT> *morton_end = (morton_sort_t<uT> *)end;
  std::sort(morton_begin, morton_end);

  header_p_adjust_to_sorted_data(tree_state, header, begin, end);
}

void sort_points(const tree_global_state_t &tree_state, points_t &points)
{
  assert(points.header.attributes.attributes[0].name_size == strlen(POINTS_ATTRIBUTE_XYZ));
  assert(memcmp(points.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ, points.header.attributes.attributes[0].name_size) == 0);

  uint64_t expected_points_buffer_size = size_for_format(points.header.attributes.attributes[0].format) * 3 * points.header.point_count;
  assert(points.buffers.buffers[0].size == expected_points_buffer_size);

  switch (points.header.attributes.attributes[0].format)
  {
  case format_i32:
    convert_and_sort<int32_t>(tree_state, points.buffers.buffers[0], points.header);
    break;
  default:
    assert(false);
  }
}
}
} // namespace points
