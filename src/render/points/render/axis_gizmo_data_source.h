/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#ifndef POINTS_AXIS_GIZMO_DATA_SOURCE_H
#define POINTS_AXIS_GIZMO_DATA_SOURCE_H

#include <points/render/export.h>
#include <points/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points::render
{
struct axis_gizmo_data_source_t;
POINTS_RENDER_EXPORT struct axis_gizmo_data_source_t *axis_gizmo_data_source_create(struct renderer_t *renderer, const double center[3], double axis_length);
POINTS_RENDER_EXPORT void axis_gizmo_data_source_destroy(struct axis_gizmo_data_source_t *gizmo);
POINTS_RENDER_EXPORT struct data_source_t axis_gizmo_data_source_get(struct axis_gizmo_data_source_t *gizmo);
POINTS_RENDER_EXPORT void axis_gizmo_data_source_set_center(struct axis_gizmo_data_source_t *gizmo, const double center[3]);
POINTS_RENDER_EXPORT void axis_gizmo_data_source_set_axis_length(struct axis_gizmo_data_source_t *gizmo, double axis_length);
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_AXIS_GIZMO_DATA_SOURCE_H
