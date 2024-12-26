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

#include <vector>
#include <memory>

#include <points/converter/converter.h>

#include "conversion_types.hpp"


namespace points::converter
{
inline int size_for_format(type_t format)
{
  switch (format)
  {
  case type_u8:
  case type_i8:
    return 1;
  case type_u16:
  case type_i16:
    return 2;
  case type_u32:
  case type_i32:
  case type_r32:
  case type_m32:
    return 4;
  case type_u64:
  case type_i64:
  case type_r64:
  case type_m64:
    return 8;
  case type_m128:
    return 16;
  case type_m192:
    return 24;
  }
  return 0;
}

inline int size_for_format(type_t format, components_t components)
{
  return size_for_format(format) * (int)components;
}
inline int size_for_format(std::pair<type_t, components_t> format)
{
  return size_for_format(format.first) * (int)format.second;
}

void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count);
void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count, std::unique_ptr<uint8_t[]> && morton_attribute_buffer);
void attribute_buffers_adjust_buffers_to_size(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count);
void attributes_copy(const attributes_t &source, attributes_t &target);
}


