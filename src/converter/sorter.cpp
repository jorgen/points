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
  double new_min[3];
  new_min[0] = std::floor(header.min[0]);
  new_min[1] = std::floor(header.min[1]);
  new_min[2] = std::floor(header.min[2]);
  double adjust[3];
  adjust[0] = header.offset[0] - new_min[0];
  adjust[1] = header.offset[1] - new_min[1];
  adjust[2] = header.offset[2] - new_min[2];
  header.offset[0] -= adjust[0];
  header.offset[1] -= adjust[1];
  header.offset[2] -= adjust[2];
  int adjust_int[3];
  adjust_int[0] = int(adjust[0]);
  adjust_int[1] = int(adjust[1]);
  adjust_int[2] = int(adjust[2]);
  adjust_int[0] /= header.scale[0];
  adjust_int[1] /= header.scale[1];
  adjust_int[2] /= header.scale[2];

  using uT = typename std::make_unsigned<T>::type;
  T *begin = (T *)buffer.data;
  T *end = begin + (header.point_count * 3);
  for (T *p = begin; p < end; p += 3)
  {
    p[0] += adjust_int[0];
    p[1] += adjust_int[1];
    p[2] += adjust_int[2];
    p[2] += adjust_int[2];
    assert(p[0] > 0 && p[1] > 0 && p[2] > 0);

    morton::encode((uT *)p);
  }

  morton_sort_t<uT> *morton_begin = (morton_sort_t<uT> *)begin;
  morton_sort_t<uT> *morton_end = (morton_sort_t<uT> *)end;
  std::sort(morton_begin, morton_end);

  morton_sort_t<uT> first = *morton_begin;
  morton_sort_t<uT> last = *(morton_end - 1);
  assert(first < last);


  header_p_adjust_to_sorted_data(tree_state, header, begin, end);
}

void sort_points(const tree_global_state_t &tree_state, const std::vector<std::pair<format_t, components_t>> &attributes_def, points_t &points)
{
  switch (attributes_def.front().first)
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
