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

enum points_type_t
{
  points_type_u8,
  points_type_i8,
  points_type_u16,
  points_type_i16,
  points_type_u32,
  points_type_i32,
  points_type_m32,
  points_type_r32,
  points_type_u64,
  points_type_i64,
  points_type_m64,
  points_type_r64,
  points_type_m128,
  points_type_m192,
};

enum points_components_t
{
  points_components_1 = 1,
  points_components_2 = 2,
  points_components_3 = 3,
  points_components_4 = 4,
  points_components_4x4 = 5
};

#ifdef __cplusplus
}
#endif
#endif //POINTS_FORMAT_H
