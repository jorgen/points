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
#include <string>

#include <points/converter/converter.h>

#include "conversion_types_p.h"

namespace points
{
namespace converter
{
inline uint64_t size_for_format(format_t format)
{
  switch (format)
  {
  case format_u8:
  case format_i8:
    return 1;
  case format_u16:
  case format_i16:
    return 2;
  case format_u32:
  case format_i32:
    return 4;
  case format_u64:
  case format_i64:
    return 8;
  case format_r32:
    return 4;
  case format_r64:
    return 8;
  }
  return 0;
}

void header_p_calculate_morton_aabb(internal_header_t &header);
void attribute_buffers_initialize(const std::vector<attribute_t> &attributes, attribute_buffers_t &buffers, uint64_t point_count);
void attribute_buffers_adjust_buffers_to_size(const std::vector<attribute_t> &attributes, attribute_buffers_t &buffers, uint64_t point_count);
void attributes_copy(const attributes_t &source, attributes_t &target);
void header_copy(const internal_header_t &source, internal_header_t &target);
}
}

