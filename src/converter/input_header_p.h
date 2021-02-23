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

#include <stdint.h>

#include <limits>
#include <vector>
#include <memory>

#include <points/converter/converter.h>

namespace points
{
namespace converter
{
struct header_t
{
  uint64_t point_count = 0;
  uint64_t data_start = 0;
  double offset[3] = {};
  double scale[3] = {};
  double min[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
  double max[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};

  uint64_t morton_min[3] = {};
  uint64_t morton_max[3] = {~uint64_t(0), ~uint64_t(0), ~uint64_t(0)};

  std::vector<attribute_t> attributes;
  std::vector<std::unique_ptr<char[]>> attribute_names;

  void *user_ptr;
};

void header_p_calculate_morton_aabb(header_t &header);
}
}

