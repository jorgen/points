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
#ifndef DEW_FLAT_POINTS_DATA_SOURCE_H
#define DEW_FLAT_POINTS_DATA_SOURCE_H

#include <dew/render/export.h>
#include <dew/render/data_source.h>
#ifdef __cplusplus
extern "C" {
#endif

struct dew_flat_points_data_source_t;
DEW_RENDER_EXPORT struct dew_flat_points_data_source_t *dew_flat_points_data_source_create(struct dew_renderer_t *renderer, const char *url, int url_size);
DEW_RENDER_EXPORT void dew_flat_points_data_source_destroy(struct dew_flat_points_data_source_t *flat_points_data_source);
DEW_RENDER_EXPORT struct dew_data_source_t dew_flat_points_data_source_get(struct dew_flat_points_data_source_t *flat_points_data_source);
DEW_RENDER_EXPORT void dew_flat_points_get_aabb(struct dew_flat_points_data_source_t *points, double aabb_min[3], double aabb_max[3]);

#ifdef __cplusplus
}
#endif
#endif //DEW_FLAT_POINT_DATA_SOURCE_H
