/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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

#include <points/common/format.h>

namespace points
{
template <typename T>
type_t type_from_type()
{
  if (std::is_same<T, uint8_t>::value)
    return type_u8;
  else if (std::is_same<T, int8_t>::value)
    return type_i8;
  else if (std::is_same<T, uint16_t>::value)
    return type_u16;
  else if (std::is_same<T, int16_t>::value)
    return type_i16;
  else if (std::is_same<T, uint32_t>::value)
    return type_u32;
  else if (std::is_same<T, int32_t>::value)
    return type_i32;
  else if (std::is_same<T, uint64_t>::value)
    return type_u64;
  else if (std::is_same<T, int64_t>::value)
    return type_i64;
  else if (std::is_same<T, float>::value)
    return type_r32;
  else if (std::is_same<T, double>::value)
    return type_r64;
  else
    assert(sizeof(T) == 0 && "Unsupported type");
  {
  }
}
} // namespace points
