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
#ifndef POINTS_CONVERTER_LASZIP_CALLBACKS_H
#define POINTS_CONVERTER_LASZIP_CALLBACKS_H

#include <points/export.h>
#include <points/converter/converter.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{
POINTS_EXPORT struct converter_file_convert_callbacks_t laszip_callbacks();
} // namespace converter
} // namespace points
#ifdef __cplusplus
}
#endif

#endif
