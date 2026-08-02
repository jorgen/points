/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#ifndef DEW_AXIS_GIZMO_DATA_SOURCE_H
#define DEW_AXIS_GIZMO_DATA_SOURCE_H

#include <dew/render/export.h>
#include <dew/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_axis_gizmo_data_source_t;
DEW_RENDER_EXPORT struct dew_axis_gizmo_data_source_t *dew_axis_gizmo_data_source_create(struct dew_renderer_t *renderer, const double center[3], double axis_length);
DEW_RENDER_EXPORT void dew_axis_gizmo_data_source_destroy(struct dew_axis_gizmo_data_source_t *gizmo);
DEW_RENDER_EXPORT struct dew_data_source_t dew_axis_gizmo_data_source_get(struct dew_axis_gizmo_data_source_t *gizmo);
DEW_RENDER_EXPORT void dew_axis_gizmo_data_source_set_center(struct dew_axis_gizmo_data_source_t *gizmo, const double center[3]);
DEW_RENDER_EXPORT void dew_axis_gizmo_data_source_set_axis_length(struct dew_axis_gizmo_data_source_t *gizmo, double axis_length);

#ifdef __cplusplus
}
#endif
#endif //DEW_AXIS_GIZMO_DATA_SOURCE_H
