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

#include <points/converter/converter.h>

#include "morton_p.h"

#include <vector>
#include <memory>
#include <string>

namespace points
{
namespace converter
{

struct attributes_t
{
  std::vector<attribute_t> attributes;
  std::vector<std::unique_ptr<char[]>> attribute_names;
};

struct attribute_buffers_t
{
  std::vector<buffer_t> buffers;
  std::vector<std::unique_ptr<uint8_t[]>> data;
};

struct internal_header_t : header_t
{
  std::string name;
  morton::morton64_t morton_min;
  morton::morton64_t morton_max;
  int lod_span;
  
  attributes_t attributes;

};

inline void internal_header_initialize(internal_header_t &header)
{
  header.point_count = 0;
  header.offset[0] = 0.0;
  header.offset[1] = 0.0;
  header.offset[2] = 0.0;
  header.scale[0] = 0.0;
  header.scale[1] = 0.0;
  header.scale[2] = 0.0;
  header.min[0] = -std::numeric_limits<double>::max();
  header.min[1] = -std::numeric_limits<double>::max();
  header.min[2] = -std::numeric_limits<double>::max();
  header.max[0] = std::numeric_limits<double>::max();
  header.max[1] = std::numeric_limits<double>::max();
  header.max[2] = std::numeric_limits<double>::max();

  morton::morton_init_min(header.morton_min);
  morton::morton_init_max(header.morton_max);
  header.lod_span = 0;
}

struct points_t
{
  internal_header_t header;
  attribute_buffers_t buffers;
};

struct tree_global_state_t
{
  uint32_t node_limit = 100000;
  double scale[3];
  double offset[3];
};
} // namespace converter
} // namespace points
