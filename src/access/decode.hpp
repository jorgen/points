/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

// Decode a node's stored morton codes into positions, for ANALYSIS rather than for drawing.
//
// Deliberately not the renderer's decode (converter/node_decode.cpp), which produces float32 relative
// to a node origin, reorders the points coarse->fine for runtime LOD, and appends a per-point
// rep_level. All three are wrong here: the reorder destroys the stored order, and float32 relative
// coordinates lose precision that an exporter or an analytics job needs. Keeping this separate is
// also what keeps dew_access free of any dependency on dew_render.

#include "dataset_types.hpp"

#include <cstdint>
#include <vector>

namespace dew::access
{
using namespace dew::core;

enum class position_format_t
{
  r64_absolute,  // double[3] in world units -- lossless, the default for analysis
  r32_relative,  // float[3] relative to the node origin reported alongside the buffer
  i32_grid,      // int32[3] in octree grid steps, i.e. the dataset's own integer coordinates
};

inline uint32_t position_stride(position_format_t format)
{
  switch (format)
  {
  case position_format_t::r64_absolute:
    return 3 * sizeof(double);
  case position_format_t::r32_relative:
    return 3 * sizeof(float);
  case position_format_t::i32_grid:
    return 3 * sizeof(int32_t);
  }
  return 0;
}

// Decode `point_count` morton codes from `morton_data` (whose width is given by `format`, the point
// format recorded in the node's storage header) into `dst`.
//
// `header_min` is the node's morton_min; stored codes are narrowed relative to the node cell, so the
// cell origin has to be restored before a code means anything globally. `origin_out` receives the
// node origin in world units, which r32_relative and i32_grid outputs are expressed against.
bool decode_positions(const void *morton_data, uint32_t data_size, uint32_t point_count, point_format_t format, const morton::morton192_t &header_min, int lod_span, const tree_config_t &tree_config,
                      position_format_t out_format, void *dst, uint64_t dst_size, double origin_out[3]);

// Keep only the points whose world position falls inside `box`, compacting positions and every
// attribute buffer in step. Returns the surviving count.
//
// This is the one thing the format cannot answer by itself: node selection is cell-granular, so a
// query that wants exactly the points inside a box has to test them individually after decode.
struct attribute_span_t
{
  uint8_t *data;
  uint32_t stride;
};
// `scale` is the dataset's grid step; i32_grid values are in grid units, so converting them back to
// world space needs it. It is ignored for the other two formats.
uint32_t clip_to_box(void *positions, position_format_t format, const double origin[3], double scale, uint32_t point_count, const double box_min[3], const double box_max[3], attribute_span_t *attributes, uint32_t attribute_count);

} // namespace dew::access
