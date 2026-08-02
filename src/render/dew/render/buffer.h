/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#ifndef DEW_BUFFER_H
#define DEW_BUFFER_H

#include <dew/render/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_buffer_t;
DEW_RENDER_EXPORT void dew_buffer_set_rendered(struct dew_buffer_t *buffer);
DEW_RENDER_EXPORT void dew_buffer_release_data(struct dew_buffer_t *buffer);

#ifdef __cplusplus
}
#endif
#endif //DEW_BUFFER_H
