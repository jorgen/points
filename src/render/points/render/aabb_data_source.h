/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#ifndef POINTS_AABB_DATA_SOURCE_H
#define POINTS_AABB_DATA_SOURCE_H

#include <points/render/export.h>
#include <points/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_aabb_data_source_t;
POINTS_RENDER_EXPORT struct points_aabb_data_source_t *points_aabb_data_source_create(struct points_renderer_t *renderer, const double offset[3]);
POINTS_RENDER_EXPORT void points_aabb_data_source_destroy(struct points_aabb_data_source_t *aabb_data_source);
POINTS_RENDER_EXPORT struct points_data_source_t points_aabb_data_source_get(struct points_aabb_data_source_t *aabb_data_source);
POINTS_RENDER_EXPORT int points_aabb_data_source_add_aabb(struct points_aabb_data_source_t *aabb_data_source, const double min[3], const double max[3]);
POINTS_RENDER_EXPORT void points_aabb_data_source_remove_aabb(struct points_aabb_data_source_t *aabb_data_source, int id);
POINTS_RENDER_EXPORT void points_aabb_data_source_modify_aabb(struct points_aabb_data_source_t *aabb_data_source, int id, const double min[3], const double max[3]);
POINTS_RENDER_EXPORT void points_aabb_data_source_get_center(struct points_aabb_data_source_t *aabb_data_source, int id, double center[3]);

#ifdef __cplusplus
}
#endif
#endif //POINTS_AABB_DATA_SOURCE_H
