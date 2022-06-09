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
#ifndef POINTS_FLAT_POINTS_DATA_SOURCE_H
#define POINTS_FLAT_POINTS_DATA_SOURCE_H

#include <points/export.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace render
{
struct flat_points_data_source_t;
POINTS_EXPORT struct flat_points_data_source_t *flat_points_data_source_create(struct renderer_t *renderer, const char *url, int url_size);
POINTS_EXPORT void flat_points_data_source_destroy(struct flat_points_data_source_t *flat_points_data_source);
POINTS_EXPORT struct data_source_t *flat_points_data_source_get(struct flat_points_data_source_t *flat_points_data_source);
POINTS_EXPORT void flat_points_get_aabb(struct flat_points_data_source_t *points, double aabb_min[3], double aabb_max[3]);
}

} // namespace points

#ifdef __cplusplus
}
#endif
#endif //POINTS_FLAT_POINT_DATA_SOURCE_H
