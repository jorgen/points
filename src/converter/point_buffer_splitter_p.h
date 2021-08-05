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

#include <points/converter/default_attribute_names.h>

namespace points
{
namespace converter
{

template<typename T>
void find_offsets(const tree_global_state_t &state, const points_t &points, const morton::morton64_t (&search_offsets)[8], uint32_t (&offsets)[8])
{

  using morton_u =  morton::morton_t<typename std::make_unsigned<T>::type>;
  morton_u morton_limit;
  assert(points.buffers.buffers[0].size / sizeof(morton_u) == points.header.point_count);
  const morton_u *morton_begin = reinterpret_cast<const morton_u *>(points.buffers.buffers[0].data);
  const morton_u *morton_end = morton_begin + points.header.point_count;
  const morton_u *morton_codes = morton_begin;
  memset(offsets, 0, sizeof(offsets));
  for (int i = 0; i < 7; i++)
  {
    double pos[3];
    morton::decode(search_offsets[i + 1], state.tree_scale, pos);
    pos[0] -= points.header.offset[0];
    pos[1] -= points.header.offset[1];
    pos[2] -= points.header.offset[2];
    morton::encode(pos, points.header.scale, morton_limit);
    morton_codes = std::lower_bound(morton_codes, morton_end, morton_limit);
    offsets[i] = uint32_t(morton_codes - morton_begin);
    if (morton_codes == morton_end)
      break;
  }
  if (morton_codes != morton_end)
  {
    offsets[7] = uint32_t(morton_end - morton_begin);
  }
}

void point_buffer_get_child_offsets(const tree_global_state_t &state, const points_t &points, int lod, uint32_t (&offsets)[8])
{
  auto mask = morton::morton_negate(morton::morton_mask_create(lod));
  morton::morton64_t morton_indices[8];
  memset(morton_indices, 0, sizeof(morton_indices));
  morton_indices[0] = morton::morton_and(points.header.morton_min, mask);
  for (int i = 1; i < 8; i++)
  {
    morton_indices[i] = morton_indices[0];
    morton::morton_set_child_mask(lod, uint8_t(i), morton_indices[i]);
  }

  assert(strcmp(points.header.attributes.attributes[0].name, POINTS_ATTRIBUTE_XYZ) == 0);
  switch(points.header.attributes.attributes[0].format)
  {
  case format_i32:
    find_offsets<int32_t>(state, points, morton_indices, offsets);
    break;
  default:
    assert(false);
    break;
  }
}

static void point_buffer_split_copy_buffers(const points_t &source, points_t &destination, int begin, int size)
{
  for (int i = 0; i < source.buffers.data.size(); i++)
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



void point_buffer_split_buffers_to_children(const tree_global_state_t &state, points_t &p, int lod, uint32_t (&offsets)[8], points_data_t (&children)[8])
{
  for (int i = 0; i < 8; i++)
  {
    int prev_offset = i == 0 ? 0 : offsets[i - 1];
    int curr_offset = offsets[i];
    int diff_offset = curr_offset - prev_offset;
    if (diff_offset == 0)
      continue;
    auto &child = children[i];
    if (diff_offset == p.header.point_count)
    {
      points_data_add(child, std::move(p));
    }
    else
    {
      child.data.emplace_back();
      auto &dest_points = children[i].data.back();
      header_copy(p.header, dest_points.header);
      point_buffer_split_copy_buffers(p, dest_points, prev_offset, diff_offset);
      p.header.point_count = diff_offset;
    }
  }
}

}
}
#endif // POINT_BUFFER_SPLITTER_H
