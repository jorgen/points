/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#ifndef DEW_FORMAT_H
#define DEW_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

enum dew_type_t
{
  dew_type_u8,
  dew_type_i8,
  dew_type_u16,
  dew_type_i16,
  dew_type_u32,
  dew_type_i32,
  dew_type_m32,
  dew_type_r32,
  dew_type_u64,
  dew_type_i64,
  dew_type_m64,
  dew_type_r64,
  dew_type_m128,
  dew_type_m192,
};

enum dew_components_t
{
  dew_components_1 = 1,
  dew_components_2 = 2,
  dew_components_3 = 3,
  dew_components_4 = 4,
  dew_components_4x4 = 5
};

#ifdef __cplusplus
}
#endif
#endif //DEW_FORMAT_H
