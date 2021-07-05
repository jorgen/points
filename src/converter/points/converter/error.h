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
#ifndef POINTS_CONVERTER_ERROR_H
#define POINTS_CONVERTER_ERROR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{
struct error_t;
POINTS_CONVERTER_EXPORT error_t *converter_error_create();
POINTS_CONVERTER_EXPORT void converter_error_destroy(error_t *error);
POINTS_CONVERTER_EXPORT void converter_error_set_info(error_t *error, int code, const char *str, size_t str_len);
POINTS_CONVERTER_EXPORT void converter_error_get_info(const error_t *error, int *code, const char **str, size_t *str_len);
} // namespace converter
} // namespace points
#ifdef __cplusplus
}
#endif

#endif
