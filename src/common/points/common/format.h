/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#ifndef POINTS_FORMAT_H
#define POINTS_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
enum type_t
{
  type_u8,
  type_i8,
  type_u16,
  type_i16,
  type_u32,
  type_i32,
  type_m32,
  type_r32,
  type_u64,
  type_i64,
  type_m64,
  type_r64,
  type_m128,
  type_m192,
};

enum components_t
{
  components_1 = 1,
  components_2 = 2,
  components_3 = 3,
  components_4 = 4,
  components_4x4 = 5
};
}
#ifdef __cplusplus
}
#endif
#endif //POINTS_FORMAT_H
