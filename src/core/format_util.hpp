/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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

// Element sizing for dew_type_t / dew_components_t. Split out of input_header.hpp, whose remaining
// API (attribute buffer allocation, attribute-set copying) belongs to the write pipeline: the
// compression codecs and every decode path need only this, and dragging the write helpers along
// would tie them to the converter.

#include <dew/core/format.h>

#include <utility>

namespace dew::core
{
inline int size_for_format(dew_type_t format)
{
  switch (format)
  {
  case dew_type_u8:
  case dew_type_i8:
    return 1;
  case dew_type_u16:
  case dew_type_i16:
    return 2;
  case dew_type_u32:
  case dew_type_i32:
  case dew_type_r32:
  case dew_type_m32:
    return 4;
  case dew_type_u64:
  case dew_type_i64:
  case dew_type_r64:
  case dew_type_m64:
    return 8;
  case dew_type_m128:
    return 16;
  case dew_type_m192:
    return 24;
  }
  return 0;
}

// NOTE: dew_components_4x4 is 5, not 16, so this returns size*5 for a 4x4 matrix. Dormant today --
// 4x4 only ever describes camera-matrix GPU buffers, whose size is passed explicitly and never
// routed through here -- but it is a trap for any future attribute that uses the 4x4 component
// kind.
inline int size_for_format(dew_type_t format, dew_components_t components)
{
  return size_for_format(format) * (int)components;
}
inline int size_for_format(std::pair<dew_type_t, dew_components_t> format)
{
  return size_for_format(format.first) * (int)format.second;
}
} // namespace dew::core
