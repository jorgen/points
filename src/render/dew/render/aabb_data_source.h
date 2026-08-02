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
#ifndef DEW_AABB_DATA_SOURCE_H
#define DEW_AABB_DATA_SOURCE_H

#include <dew/render/export.h>
#include <dew/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_aabb_data_source_t;
DEW_RENDER_EXPORT struct dew_aabb_data_source_t *dew_aabb_data_source_create(struct dew_renderer_t *renderer, const double offset[3]);
DEW_RENDER_EXPORT void dew_aabb_data_source_destroy(struct dew_aabb_data_source_t *aabb_data_source);
DEW_RENDER_EXPORT struct dew_data_source_t dew_aabb_data_source_get(struct dew_aabb_data_source_t *aabb_data_source);
DEW_RENDER_EXPORT int dew_aabb_data_source_add_aabb(struct dew_aabb_data_source_t *aabb_data_source, const double min[3], const double max[3]);
DEW_RENDER_EXPORT void dew_aabb_data_source_remove_aabb(struct dew_aabb_data_source_t *aabb_data_source, int id);
DEW_RENDER_EXPORT void dew_aabb_data_source_modify_aabb(struct dew_aabb_data_source_t *aabb_data_source, int id, const double min[3], const double max[3]);
DEW_RENDER_EXPORT void dew_aabb_data_source_get_center(struct dew_aabb_data_source_t *aabb_data_source, int id, double center[3]);

#ifdef __cplusplus
}
#endif
#endif //DEW_AABB_DATA_SOURCE_H
