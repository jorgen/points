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
#include "input_header.hpp"
#include "morton_tree_coordinate_transform.hpp"
#include "storage_handler.hpp"
#include "tree.hpp"

#include <points/converter/default_attribute_names.h>

#include <fmt/printf.h>

namespace points
{
namespace converter
{
template <typename T, size_t C>
void verify_points_range(const read_only_points_t &points, int start_index, int end_index, const morton::morton192_t &min, const morton::morton192_t &max)
{
  morton::morton_t<T, C> morton_current;
  morton::morton_t<T, C> morton_previous = {};
  int count_less = 0;
  int count_greater = 0;
  int wrong_order = 0;
  morton::morton_t<T, C> local_min;
  morton::morton_downcast(min, local_min);
  morton::morton_t<T, C> local_max;
  morton::morton_downcast(max, local_max);
  const auto *morton_begin = static_cast<const morton::morton_t<T, C> *>(points.data.data);
  for (int i = start_index; i < end_index; i++)
  {
    morton_current = morton_begin[i];
    // assert(!(morton_current < morton_previous));
    if (morton_current < local_min)
      count_less++;
    if (!(morton_current < local_max))
      count_greater++;
    if (!(morton_current < morton_previous))
      wrong_order++;
    morton_previous = morton_current;
  }
  assert(count_less == 0);
  assert(count_greater == 0);
  assert(wrong_order);
}

template <typename T, size_t C>
void verify_points_less_than(const tree_config_t &tree_config, const read_only_points_t &points, int start_index, int end_index, const morton::morton192_t &max)
{
  morton::morton_t<T, C> local_max;
  convert_world_morton_to_local(max, local_max);
  const auto *morton_begin = static_cast<const morton::morton_t<T, C> *>(points.data.data);
  int count = 0;

  for (int i = start_index; i < end_index; i++)
  {
    if (!(morton_begin[i] < local_max))
      count++;
  }
  assert(count == 0);
}

static void add_subset_to_child(input_data_id_t input_id, offset_in_subset_t offset, point_count_t point_count, const morton::morton192_t &start, const morton::morton192_t &end, points_collection_t &child)
{
  if (child.point_count == 0)
  {
    child.min = start;
    child.max = end;
    child.min_lod = morton::morton_lod(start, end);
  }
  else
  {
    bool modified = false;
    if (start < child.min)
    {
      child.min = start;
      modified = true;
    }
    if (child.max < end)
    {
      child.max = end;
      modified = true;
    }

    if (modified)
    {
      child.min_lod = morton::morton_lod(child.min, child.max);
    }
  }
  child.data.emplace_back(input_id, offset, point_count);
  child.point_count += point_count.data;
}

template <typename T, size_t C>
void point_buffer_subdivide_type(const read_only_points_t &points, input_storage_map_t &storage_map, const points_subset_t &subset, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  assert(points.data.size / sizeof(morton::morton_t<T, C>) == points.header.point_count);
  const morton::morton_t<T, C> *morton_begin = static_cast<const morton::morton_t<T, C> *>(points.data.data) + subset.offset.data;
  const morton::morton_t<T, C> *morton_end = morton_begin + subset.count.data;
  const morton::morton_t<T, C> *morton_current_start = morton_begin;
  const morton::morton_t<T, C> *morton_current_end = nullptr;
  assert(*morton_begin <= *(morton_end - 1));

  morton::morton_t<T, C> local_node_min;
  morton::morton_downcast(node_min, local_node_min);

#ifndef NDEBUG
  morton::morton192_t node_max = morton::morton_or(node_min, morton::morton_mask_create<uint64_t, 3>(lod));
  assert(points.header.morton_min < node_max);
  morton::morton_t<T, C> local_node_max;
  morton::morton_downcast(node_max, local_node_max);
  assert(*morton_begin >= local_node_min);
  assert(*(morton_end - 1) <= local_node_max);
#endif

  if (lod * 3 + 3 > int(sizeof(T) * 8 * C))
  {
    auto child = morton::morton_get_child_mask(lod, points.header.morton_min);
    assert(uint32_t(points.header.point_count) == subset.count.data);
    assert(child < 8);
    storage_map.add_ref(subset.input_id);
    add_subset_to_child(subset.input_id, offset_in_subset_t(0), point_count_t(uint32_t(points.header.point_count)), points.header.morton_min, points.header.morton_max, children[child]);
  }
  else
  {
    morton::morton_t<T, C> node_mask;
    for (int i = 0; i < 8 && morton_current_start != morton_end; i++)
    {
      if (i == 7)
      {
        auto mask = morton::morton_mask_create<T, C>(lod);
        node_mask = morton::morton_or(local_node_min, mask);
        morton::morton_add_one(node_mask);
      }
      else
      {
        node_mask = local_node_min;
        morton::morton_set_child_mask(lod, uint8_t(i + 1), node_mask);
      }

      if (*morton_current_start >= node_mask)
        continue;

      morton_current_end = std::lower_bound(morton_current_start, morton_end, node_mask);
      if (morton_current_end == morton_current_start)
        continue;

      auto new_offset = morton_current_start - static_cast<const morton::morton_t<T, C> *>(points.data.data);
      auto new_size = morton_current_end - morton_current_start;
      morton::morton192_t global_current_start;
      convert_local_morton_to_world(*morton_current_start, node_min, global_current_start);
      morton::morton192_t global_current_end;
      convert_local_morton_to_world(*(morton_current_end - 1), node_min, global_current_end);
      assert(new_size != 0);
      assert(new_offset >= 0 && new_offset <= std::numeric_limits<decltype(offset_in_subset_t().data)>::max());
      storage_map.add_ref(subset.input_id);
      add_subset_to_child(subset.input_id, offset_in_subset_t(uint32_t(new_offset)), point_count_t(uint32_t(new_size)), global_current_start, global_current_end, children[i]);

#ifndef NDEBUG
      verify_points_range<T, C>(points, int(new_offset), int(new_offset + new_size), node_min, node_max);
#endif
      morton_current_start = morton_current_end;
    }
    assert(morton_current_start == morton_end);
  }
}

inline void point_buffer_subdivide(const read_only_points_t &points, input_storage_map_t &storage_map, const points_subset_t &subset, int lod, const morton::morton192_t &node_min, points_collection_t (&children)[8])
{
  switch (points.header.point_format.type)
  {
  case type_m32:
    point_buffer_subdivide_type<morton::morton32_t::component_type, morton::morton32_t::component_count::value>(points, storage_map, subset, lod, node_min, children);
    break;
  case type_m64:
    point_buffer_subdivide_type<morton::morton64_t::component_type, morton::morton64_t::component_count::value>(points, storage_map, subset, lod, node_min, children);
    break;
  case type_m128:
    point_buffer_subdivide_type<morton::morton128_t::component_type, morton::morton128_t::component_count::value>(points, storage_map, subset, lod, node_min, children);
    break;
  case type_m192:
    point_buffer_subdivide_type<morton::morton192_t::component_type, morton::morton192_t::component_count::value>(points, storage_map, subset, lod, node_min, children);
    break;
  default:
    assert(false);
    break;
  }
}

} // namespace converter
} // namespace points
