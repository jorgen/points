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
#pragma once

#include "conversion_types.hpp"
#include "tree.hpp"
#include "input_header.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include "cache_file_handler.hpp"

#include <points/converter/default_attribute_names.h>

#include <fmt/printf.h>

namespace points
{
namespace converter
{

template<typename T>
void get_first_and_last_point(const tree_global_state_t &state, const points_t &points, double (&pos_min), double (&pos_max), morton::morton64_t &morton_min, morton::morton64_t &morton_max)
{
  using uint_t = typename std::make_unsigned<T>::type;
  using morton_u =  morton::morton_t<uint_t>;
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.buffers.buffers[0].data);
  const morton_u *morton_last = morton_begin + (points.header.point_count -1);

  uint_t tmp[3];

  morton::decode(morton_begin->data, tmp);

}

template<typename T>
void verify_points_range(const tree_global_state_t &state, const read_points_t &points, int start_index, int end_index, const morton::morton64_t &min, const morton::morton64_t &max)
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.data.data);
  morton_u morton_current;
  int count_less = 0;
  int count_greater = 0;
  double max_pos[3];
  convert_morton_to_pos(state.scale, state.offset, max, max_pos);
  for (int i = start_index; i < end_index; i++)
  {
    morton_current = morton_begin[i];
    morton::morton64_t current_world;
    convert_local_morton_to_world(points.header, morton_current, state, current_world);
    if (current_world < min)
      count_less++;
    if (!(current_world < max))
      count_greater++;
  }
  assert(count_less == 0);
  assert(count_greater == 0);
}

template<typename T>
void verify_points_less_than(const tree_global_state_t &state, const read_points_t &points, int start_index, int end_index, const morton::morton64_t &max)
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.data.data);
  morton::morton64_t current;
  int count = 0;

  for (int i = start_index; i < end_index; i++)
  {
    convert_local_morton_to_world(points.header, morton_begin[i], state, current);
    if (!(current < max))
      count++;
  }
  assert(count == 0);
}

template<class ForwardIt, class Compare>
ForwardIt inhouse_lower_bound(ForwardIt first, ForwardIt last, Compare comp)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);
 
    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (comp(*it)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

template<typename T>
void point_buffer_subdivide_type(const tree_global_state_t &state, const read_points_t &points, const points_subset_t &subset, int lod, const morton::morton64_t &node_min, points_collection_t (&children)[8])
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  //morton_u morton_limit;
  assert(points.data.size / sizeof(morton_u) == points.header.point_count);
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.data.data) + subset.offset;
  const morton_u *morton_end = morton_begin + points.data.size;
  const morton_u *morton_current_start = morton_begin;
  const morton_u *morton_current_end = nullptr;

  assert(!(points.header.morton_min < node_min)); // less or equal
  for (int i = 0; i < 8 && morton_current_start != morton_end; i++)
  {
    morton::morton64_t node_mask = node_min;
    morton::morton_set_child_mask(lod, uint8_t(i + 1), node_mask); 

    if (points.header.morton_max < node_mask)
    {
      morton_current_end = morton_end;
    }
    else if (!(node_mask < points.header.morton_min))
    {
      //  if (convert_world_morton_to_local(state, node_mask, points, morton_limit))
      //  {
      //    morton_codes = std::lower_bound(morton_codes, morton_end, morton_limit);
      //  }
      //  else
      {
        morton_current_end = inhouse_lower_bound(morton_current_start, morton_end, [&node_mask, &points, &state](const morton_u &value) {
          morton::morton64_t value_world;
          convert_local_morton_to_world(points.header, value, state, value_world);
          return value_world < node_mask;
        });
      }
    }

    auto new_offset = morton_current_start - morton_begin + subset.offset;
    auto new_size = morton_current_end - morton_current_start;
    children[i].data.emplace_back(subset.input_id, new_offset, new_size);

    children[i].point_count += new_size;

    morton::morton64_t global_start;
    convert_local_morton_to_world(points.header, *morton_current_start, state, global_start);
    morton::morton64_t global_end;
    convert_local_morton_to_world(points.header, *morton_current_end, state, global_end);
    bool modified = false;
    if (global_start < children[i].min)
    {
      children[i].min = global_start;
      modified = true;
    }
    if (children[i].max < global_end)
    {
      children[i].max = global_end;
      modified = true;
    }

    if (modified)
    {
      children[i].min_lod = morton::morton_lod(children[i].min, children[i].max);
    }
    //morton::morton_set_child_mask(lod, uint8_t(i), child_min);
    //verify_points_range<T>(state, points, int(morton_codes - morton_begin), offsets[i], child_min, node_mask);
    morton_current_start = morton_current_end;
  }
  assert(morton_current_start == morton_end);
}

void point_buffer_subdivide(const tree_global_state_t &state, const read_points_t &points, const points_subset_t &subset, int lod, const morton::morton64_t &node_min, points_collection_t (&children)[8])
{
  switch(points.format)
  {
  case format_i32:
    point_buffer_subdivide_type<int32_t>(state, points, subset, lod, node_min, children);
    break;
  default:
    assert(false);
    break;
  }
}

}
}
