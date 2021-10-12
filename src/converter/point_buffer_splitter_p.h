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
#ifndef POINT_BUFFER_SPLITTER_H
#define POINT_BUFFER_SPLITTER_H

#include "conversion_types_p.h"
#include "tree_p.h"
#include "input_header_p.h"
#include "morton_tree_coordinate_transform_p.h"

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
void verify_points_range(const tree_global_state_t &state, const points_t &points, int start_index, int end_index, const morton::morton64_t &min, const morton::morton64_t &max)
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.buffers.buffers[0].data);
  morton_u morton_current;
  int count_less = 0;
  int count_greater = 0;
  double max_pos[3];
  convert_morton_to_pos(state.scale, state.offset, max, max_pos);
  for (int i = start_index; i < end_index; i++)
  {
    morton_current = morton_begin[i];
    morton::morton64_t current_world;
    convert_local_morton_to_world(points, morton_current, state, current_world);
    if (current_world < min)
      count_less++;
    if (!(current_world < max))
      count_greater++;
  }
  assert(count_less == 0);
  assert(count_greater == 0);
}

template<typename T>
void verify_points_less_than(const tree_global_state_t &state, const points_t &points, int start_index, int end_index, const morton::morton64_t &max)
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.buffers.buffers[0].data);
  morton::morton64_t current;
  int count = 0;

  for (int i = start_index; i < end_index; i++)
  {
    convert_local_morton_to_world(points, morton_begin[i], state, current);
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
void find_offsets(const tree_global_state_t &state, const points_t &points, int lod, const morton::morton64_t &node_min, uint32_t (&offsets)[8])
{
  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  morton_u morton_limit;
  assert(points.buffers.buffers[0].size / sizeof(morton_u) == points.header.point_count);
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.buffers.buffers[0].data);
  const morton_u *morton_end = morton_begin + points.header.point_count;
  const morton_u *morton_codes = morton_begin;
  memset(offsets, 0, sizeof(offsets));

  assert(!(points.header.morton_min < node_min)); // less or equal
  for (int i = 0; i < 7; i++)
  {
    if (morton_codes == morton_end)
    {
      offsets[i] = uint32_t(morton_end - morton_begin);
    }
    else
    {
      morton::morton64_t node_mask = node_min;
      morton::morton_set_child_mask(lod, uint8_t(i + 1), node_mask); 
      double pos[3];
      convert_morton_to_pos(state.scale, state.offset, node_mask, pos);
      if (!(node_mask < points.header.morton_min))
      {
        if (convert_world_morton_to_local(state, node_mask, points, morton_limit))
        {
          morton_codes = std::lower_bound(morton_codes, morton_end, morton_limit);
        }
        else
        {
          morton_codes = inhouse_lower_bound(morton_codes, morton_end, [&node_mask, &points, &state](const morton_u &value) {
            morton::morton64_t value_world;
            convert_local_morton_to_world(points, value, state, value_world);
            return value_world < node_mask;
          });
        }
      }
      else if (points.header.morton_max < node_mask)
      {
        morton_codes = morton_end; 
      }
      offsets[i] = uint32_t(morton_codes - morton_begin);
      morton::morton64_t child_min = node_min;
      morton::morton_set_child_mask(lod, uint8_t(i), child_min); 
      verify_points_range<T>(state, points, int(morton_codes - morton_begin), offsets[i], child_min, node_mask);
    }
  }
  offsets[7] = uint32_t(morton_end - morton_begin);
  if (morton_codes != morton_end)
  {
    morton::morton64_t node_max = morton::morton_or(node_min, morton::morton_mask_create(lod));
    morton::morton64_t node_mask = node_max;
    morton::morton_add_one(node_mask);
    morton::morton64_t child_min = node_min;
    morton::morton_set_child_mask(lod, uint8_t(7), child_min); 
    verify_points_range<T>(state, points, int( morton_codes - morton_begin), int(points.header.point_count), child_min, node_mask);
  }
}

void point_buffer_get_child_offsets(const tree_global_state_t &state, const points_t &points, int lod, const morton::morton64_t &node_min, uint32_t (&offsets)[8])
{
  assert(strcmp(points.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ) == 0);
  switch(points.header.attributes.attributes[0].format)
  {
  case format_i32:
    find_offsets<int32_t>(state, points, lod, node_min, offsets);
    break;
  default:
    assert(false);
    break;
  }
}

static void point_buffer_split_copy_buffers(const points_t &source, points_t &destination, int begin, int size)
{
  for (int i = 0; i < int(source.buffers.data.size()); i++)
  {
    auto components = source.header.attributes.attributes[i].components;
    auto format_size = size_for_format(source.header.attributes.attributes[i].format);
    int element_size = components * format_size;
    int copy_size = element_size * size;
    int byte_offset = begin * element_size;
    destination.buffers.data.emplace_back(new uint8_t[copy_size]);
    destination.buffers.buffers.emplace_back();
    destination.buffers.buffers.back().data = destination.buffers.data.back().get();
    destination.buffers.buffers.back().size = copy_size;
    memcpy(destination.buffers.buffers.back().data, source.buffers.data[i].get() + byte_offset, copy_size);
  }
}


template<typename T>
void point_buffer_split_set_min_max_header_values(const tree_global_state_t &state, points_t &points)
{
  using uT = typename std::make_unsigned<T>::type;
  uT *begin = (uT *)points.buffers.buffers[0].data;
  uT *end = begin + (points.header.point_count * 3);
  header_p_adjust_to_sorted_data(state, points.header, begin, end);
}


void point_buffer_split_buffers_to_children(const tree_global_state_t &state, points_t &p, uint32_t (&offsets)[8], points_data_t (&children)[8])
{
  for (int i = 0; i < 8; i++)
  {
    int prev_offset = i == 0 ? 0 : offsets[i - 1];
    int curr_offset = offsets[i];
    uint64_t diff_offset = uint64_t(curr_offset - prev_offset);
    if (diff_offset == 0)
      continue;
    auto &child = children[i];
    if (diff_offset == p.header.point_count)
    {
      points_data_add(child, std::move(p));
      return;
    }
    else
    {
      child.data.emplace_back();
      auto &dest_points = children[i].data.back();
      point_buffer_split_copy_buffers(p, dest_points, prev_offset, int(diff_offset));
      header_copy(p.header, dest_points.header);
      dest_points.header.point_count = diff_offset;
      switch (dest_points.header.attributes.attributes[0].format)
      {
      case format_i32:
        point_buffer_split_set_min_max_header_values<int32_t>(state, dest_points);
        break;
      default:
        assert(false);
        break;
      }
      assert(!(dest_points.header.morton_min < p.header.morton_min));
      assert(!(p.header.morton_max < dest_points.header.morton_max));


      assert(dest_points.header.lod_span <= p.header.lod_span);
      if (child.point_count == 0)
      {
        memcpy(child.morton_min.data, dest_points.header.morton_min.data, sizeof(child.morton_min)); 
        memcpy(child.morton_max.data, dest_points.header.morton_max.data, sizeof(child.morton_max)); 
        child.min_lod = dest_points.header.lod_span;
        child.point_count = dest_points.header.point_count;
      }
      else
      {
        points_data_adjust_to_points(child, dest_points);
      }
    }
  }
}

}
}
#endif // POINT_BUFFER_SPLITTER_H
