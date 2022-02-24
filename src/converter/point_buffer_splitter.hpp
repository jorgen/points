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
template<typename T, size_t C>
void verify_points_range(const tree_global_state_t &state, const read_points_t &points, int start_index, int end_index, const morton::morton192_t &min, const morton::morton192_t &max)
{
  morton::morton_t<T,C> morton_current;
  morton::morton_t<T,C> morton_previous = {};
  int count_less = 0;
  int count_greater = 0;
  int wrong_order = 0;
  morton::morton_t<T,C> local_min;
  convert_world_morton_to_local(min, points.header.lod_span, local_min);
  morton::morton_t<T,C> local_max;
  convert_world_morton_to_local(max, points.header.lod_span, local_max);
  const morton::morton_t<T,C> *morton_begin = static_cast<const morton::morton_t<T,C> *>(points.data.data);
  for (int i = start_index; i < end_index; i++)
  {
    morton_current = morton_begin[i];
    assert(morton_previous < morton_current);
    if (morton_current < local_min)
      count_less++;
    if (!(morton_current < local_max))
      count_greater++;
    if (morton_current < morton_previous)
      wrong_order++;
    morton_previous = morton_current;
  }
  assert(count_less == 0);
  assert(count_greater == 0);
  assert(wrong_order);
}

template<typename T, size_t C>
void verify_points_less_than(const tree_global_state_t &state, const read_points_t &points, int start_index, int end_index, const morton::morton192_t &max)
{
  morton::morton_t<T,C> local_max;
  convert_world_morton_to_local(max, local_max);
  const morton::morton_t<T,C> *morton_begin = static_cast<const morton::morton_t<T,C> *>(points.data.data);
  int count = 0;

  for (int i = start_index; i < end_index; i++)
  {
    if (!(morton_begin[i] < local_max))
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

template<typename T, size_t C>
void point_buffer_subdivide_type(const tree_global_state_t &state, const read_points_t &points, const points_subset_t &subset, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  assert(points.data.size / sizeof(morton::morton_t<T,C>) == points.header.point_count);
  const morton::morton_t<T,C>*morton_begin = static_cast<const morton::morton_t<T,C>*>(points.data.data) + subset.offset;
  const morton::morton_t<T,C>*morton_end = morton_begin + subset.size;
  const morton::morton_t<T,C>*morton_current_start = morton_begin;
  const morton::morton_t<T,C>*morton_current_end = nullptr;
  assert(*morton_begin < *(morton_end - 1));

  morton::morton_t<T,C> local_node_min;
  convert_world_morton_to_local(node_min, local_node_min);
  morton::morton_t<T,C> node_mask;
  //morton::morton_t<T,C> node_max;
  //morton::morton_add_one(node_max);
  //verify_points_range<T, C>(state, points, subset.offset, subset.offset + subset.size, node_min, node_max);

  for (int i = 0; i < 8 && morton_current_start != morton_end; i++)
  {
    if (i == 7)
    {
      node_mask = morton::morton_or(local_node_min, morton::morton_mask_create<T,C>(lod));
      morton::morton_add_one(node_mask);
    }
    else
    {
      node_mask = local_node_min;
      morton::morton_set_child_mask(lod, uint8_t(i + 1), node_mask);
    }

    if (node_mask< *morton_current_start)
      continue;

    morton_current_end = std::lower_bound(morton_current_start, morton_end, node_mask);
    if (morton_current_end == morton_current_start)
      continue;

    auto new_offset = morton_current_start - morton_begin;
    auto new_size = morton_current_end - morton_current_start;
    if (new_size == 0)
    {
      fmt::print(stderr, "wtf\n");
    }
    morton::morton192_t global_current_start;
    convert_local_morton_to_world(*morton_current_start, node_min, global_current_start);
    morton::morton192_t global_current_end;
    convert_local_morton_to_world(*(morton_current_end - 1), node_min, global_current_end);
    auto &child = children[i];
    if (child.point_count == 0)
    {
      child.min = global_current_start;
      child.max = global_current_end;
      children[i].min_lod = morton::morton_lod(children[i].min, children[i].max);
    }
    else
    {
      bool modified = false;
      if (global_current_start < child.min)
      {
        child.min = global_current_start;
        modified = true;
      }
      if (child.max < global_current_end)
      {
        child.max = global_current_end;
        modified = true;
      }

    if (modified)
    {
      child.min_lod = morton::morton_lod(child.min, child.max);
    }
    }

    child.data.emplace_back(subset.input_id, new_offset, new_size);
    child.point_count += new_size;

    //morton::morton_set_child_mask(lod, uint8_t(i), child_min);
    //verify_points_range<T>(state, points, int(morton_codes - morton_begin), offsets[i], child_min, node_mask);

    morton_current_start = morton_current_end;
    if (morton_current_start != morton_end)
      convert_local_morton_to_world(*morton_current_start, node_min, global_current_start);
  }
  assert(morton_current_start == morton_end);
}

inline void point_buffer_subdivide(const tree_global_state_t &state, const read_points_t &points, const points_subset_t &subset, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  switch(points.header.point_format)
  {
  case format_m32:
    point_buffer_subdivide_type<morton::morton32_t::component_type, morton::morton32_t::component_count::value>(state, points, subset, lod, node_min, children);
    break;
  case format_m64:
    point_buffer_subdivide_type<morton::morton64_t::component_type, morton::morton64_t::component_count::value>(state, points, subset, lod, node_min, children);
    break;
  case format_m128:
    point_buffer_subdivide_type<morton::morton128_t::component_type, morton::morton128_t::component_count::value>(state, points, subset, lod, node_min, children);
    break;
  case format_m192:
    point_buffer_subdivide_type<morton::morton192_t::component_type, morton::morton192_t::component_count::value>(state, points, subset, lod, node_min, children);
    break;
  default:
    assert(false);
    break;
  }
}

}
}
