/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

#include "frustum_tree_walker.hpp"       // node_aabb_t
#include "morton.hpp"                     // morton192_t
#include "point_buffer_render_helper.hpp" // dyn_points_data_handler_t

#include <dew/core/format.h> // dew_type_t

#include <array>
#include <cstdint>
#include <memory>

namespace dew::converter
{
using namespace dew::core;

// A downloaded leaf kept resident in CPU memory so the renderer can grow a virtual octree from it without
// re-reading from storage. Holds the PRE-reorder morton-sorted code array (the octant split needs morton
// contiguity, which the uploaded coarse->fine buffer destroys) plus one morton-ORDER r32x3 decode of the leaf
// (so each virtual node's vertex buffer is a cheap slice/gather of it -- no per-node re-decode).
struct resident_source_t
{
  std::shared_ptr<dyn_points_data_handler_t> data_handler; // owns data_info[0]=morton codes + header + attrs
  std::shared_ptr<uint8_t[]> decoded_vertex;               // morton-order packed r32x3, 1:1 with the codes
  std::array<double, 3> decode_offset = {};                // origin the decode is relative to (per-slice)
  morton::morton192_t node_min = {};                       // the leaf's loose-cube morton_min (split origin)
  node_aabb_t leaf_loose_aabb = {};                        // walker_data.aabb of the leaf
  int leaf_lod = 0;                                        // walker_data.lod of the leaf
  dew_type_t morton_type = dew_type_m64;             // header.point_format.type
  uint32_t point_count = 0;
  size_t cpu_bytes = 0;                                    // codes + decoded_vertex + attrs, for CPU budget
};

} // namespace dew::converter
