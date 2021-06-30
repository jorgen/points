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
};

struct tree_t
{
  morton::morton64_t morton_min;
  morton::morton64_t morton_max;
  std::vector<uint8_t> nodes[5];
  std::vector<int16_t> skips[5];
  std::vector<points_data_t> data[5];
  std::vector<tree_t> sub_trees;
  uint32_t node_limit;
  uint8_t level;
};

void tree_initialize(tree_t &tree, int node_limit, points_t &&points);
void tree_add_points(tree_t &tree, points_t &&points);
}
}

