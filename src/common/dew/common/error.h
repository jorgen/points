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
#ifndef DEW_ERROR_H
#define DEW_ERROR_H
#include <dew/common/export.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

struct dew_error_t;
DEW_COMMON_EXPORT struct dew_error_t *dew_error_create(void);
DEW_COMMON_EXPORT void dew_error_destroy(struct dew_error_t *error);
DEW_COMMON_EXPORT void dew_error_set_info(struct dew_error_t *error, int code, const char *str, size_t str_len);
DEW_COMMON_EXPORT void dew_error_get_info(const struct dew_error_t *error, int *code, const char **str, size_t *str_len);

#ifdef __cplusplus
}
#endif

#endif
