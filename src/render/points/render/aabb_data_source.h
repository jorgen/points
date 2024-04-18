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
#include <points/render/aabb.h>
#include <points/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points::render
{
struct aabb_data_source_t;
POINTS_RENDER_EXPORT struct aabb_data_source_t *aabb_data_source_create(struct renderer_t *renderer, const double offset[3]);
POINTS_RENDER_EXPORT void aabb_data_source_destroy(struct aabb_data_source_t *aabb_data_source);
POINTS_RENDER_EXPORT struct data_source_t aabb_data_source_get(struct aabb_data_source_t *aabb_data_source);
POINTS_RENDER_EXPORT int aabb_data_source_add_aabb(struct aabb_data_source_t *aabb_data_source, const double min[3], const double max[3]);
POINTS_RENDER_EXPORT void aabb_data_source_remove_aabb(struct aabb_data_source_t *aabb_data_source, int id);
POINTS_RENDER_EXPORT void aabb_data_source_modify_aabb(struct aabb_data_source_t *aabb_data_source, int id, const double min[3], const double max[3]);
POINTS_RENDER_EXPORT void aabb_data_source_get_center(struct aabb_data_source_t *aabb_data_source, int id, double center[3]);
}



#ifdef __cplusplus
}
#endif
#endif //POINTS_AABB_DATA_SOURCE_H
