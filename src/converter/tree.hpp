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

#include <vector>
#include <list>

#include "conversion_types.hpp"
#include "morton.hpp"

namespace points
{
namespace converter
{

class cache_file_handler_t;

struct points_collection_t
{
  uint64_t point_count = 0;
  morton::morton192_t min;
  morton::morton192_t max;
  int min_lod;
  std::vector<points_subset_t> data;
};

inline void points_data_initialize(points_collection_t &to_init, const storage_header_t &header)
{
  to_init.point_count = header.public_header.point_count;
  to_init.data.emplace_back(header.input_id, offset_t(0), point_count_t(header.public_header.point_count));
  to_init.max = header.morton_max;
  to_init.min = header.morton_min;
  to_init.min_lod = header.lod_span;
  assert(morton::morton_lod(to_init.min, to_init.max) == to_init.min_lod);
}

inline void points_data_add(points_collection_t &dest, points_collection_t &&to_add)
{
  if (dest.point_count == 0)
  {
    dest = std::move(to_add);
    return;
  }
  for (auto &p : to_add.data)
    dest.data.emplace_back(std::move(p));
  dest.point_count += to_add.point_count;
  if (to_add.min < dest.min)
    dest.min = to_add.min;
  if (dest.max < to_add.max)
    dest.max = to_add.max;
  dest.min_lod = morton::morton_lod(dest.min, dest.max);
}

inline void points_data_add(points_collection_t &dest, const storage_header_t &to_add)
{
  if (dest.point_count == 0)
  {
    points_data_initialize(dest, to_add);
    return;
  }
  dest.data.emplace_back(to_add.input_id, offset_t(0), point_count_t(to_add.public_header.point_count));
  dest.point_count += to_add.public_header.point_count;
  if (to_add.morton_min < dest.min)
    dest.min = to_add.morton_min;
  if (dest.max < to_add.morton_max)
    dest.max = to_add.morton_max;
  dest.min_lod = morton::morton_lod(dest.min, dest.max);
}

struct tree_id_t
{
  uint32_t data;
};

struct tree_t
{
  morton::morton192_t morton_min;
  morton::morton192_t morton_max;
  std::vector<uint8_t> nodes[5];
  std::vector<int16_t> skips[5];
  std::vector<points_collection_t> data[5];
  std::vector<tree_id_t> sub_trees;
#ifndef NDEBUG
  std::vector<morton::morton192_t> mins[5];
#endif
  tree_id_t id;
  uint8_t magnitude;
};

struct tree_cache_t
{
  std::vector<tree_t> data;
  uint32_t current_id;
  uint64_t current_lod_node_id;
  tree_t *get(tree_id_t id) { return &data[id.data]; }

};

tree_id_t tree_initialize(const tree_global_state_t &global_state, tree_cache_t &tree_cache, cache_file_handler_t &cache, const storage_header_t &header);
tree_id_t tree_add_points(const tree_global_state_t &global_state, tree_cache_t &tree_cache, cache_file_handler_t &cache, tree_id_t &tree_id, const storage_header_t &header);
}
}

