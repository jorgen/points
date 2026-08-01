/************************************************************************
** dewfall - point cloud management software.
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
#ifndef DEW_CONVERTER_LASZIP_CALLBACKS_H
#define DEW_CONVERTER_LASZIP_CALLBACKS_H

#include <dew/converter/export.h>
#include <dew/converter/converter.h>

#ifdef __cplusplus
extern "C" {
#endif

DEW_CONVERTER_EXPORT struct dew_converter_file_convert_callbacks_t dew_laszip_callbacks(void);

#ifdef __cplusplus
}
#endif

#endif
