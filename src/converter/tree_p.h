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

#include "conversion_types_p.h"
#include "morton_p.h"

namespace points
{
namespace converter
{
struct points_data_t
{
  uint64_t point_count = 0;
  std::vector<points_t> data;
  morton::morton64_t morton_min;
  morton::morton64_t morton_max;
  int lod_span = 0;
};

inline void points_data_initialize(points_data_t &to_init, points_t &&p)
{
  to_init.morton_max = p.header.morton_max;
  to_init.morton_min = p.header.morton_min;
  to_init.lod_span = p.header.lod_span;
  to_init.point_count = p.header.point_count;
  to_init.data.emplace_back(std::move(p));
}

inline void points_data_add(points_data_t &dest, points_data_t &&to_add)
{
  if (dest.point_count == 0)
  {
    dest = std::move(to_add);
    return;
  }
  if (to_add.morton_min < dest.morton_min)
    dest.morton_min = to_add.morton_min;
  if (dest.morton_max < to_add.morton_max)
    dest.morton_max = to_add.morton_max;
  dest.lod_span = morton::morton_lod(dest.morton_min, dest.morton_max);
  for (auto &p : to_add.data)
    dest.data.emplace_back(std::move(p));
  dest.point_count += to_add.point_count;
}

inline void points_data_adjust_to_points(points_data_t &dest, const points_t &adjust_to)
{
  if (adjust_to.header.morton_min < dest.morton_min)
    dest.morton_min = adjust_to.header.morton_min;
  if (dest.morton_max < adjust_to.header.morton_max)
    dest.morton_max = adjust_to.header.morton_max;
  dest.lod_span = morton::morton_lod(dest.morton_min, dest.morton_max);
  dest.point_count += adjust_to.header.point_count;
}

inline void points_data_add(points_data_t &dest, points_t &&to_add)
{
  if (dest.point_count == 0)
  {
    points_data_initialize(dest, std::move(to_add));
    return;
  }
  points_data_adjust_to_points(dest, to_add);
  dest.data.emplace_back(std::move(to_add));
}

struct tree_t
{
  morton::morton64_t morton_min;
  morton::morton64_t morton_max;
  std::vector<uint8_t> nodes[5];
  std::vector<int16_t> skips[5];
  std::vector<points_data_t> data[5];
  std::vector<tree_t> sub_trees;
  uint8_t level;
};

void tree_initialize(const tree_global_state_t &state, tree_t &tree, points_t &&points);
void tree_add_points(const tree_global_state_t &state, tree_t &tree, points_t &&points);
}
}

