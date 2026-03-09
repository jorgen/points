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
#ifndef POINTS_ENVIRONMENT_DATA_SOURCE_H
#define POINTS_ENVIRONMENT_DATA_SOURCE_H

#include <points/render/export.h>
#include <points/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_environment_data_source_t;
POINTS_RENDER_EXPORT struct points_environment_data_source_t *points_environment_data_source_create(struct points_renderer_t *renderer, double ground_z, double grid_size);
POINTS_RENDER_EXPORT void points_environment_data_source_destroy(struct points_environment_data_source_t *env);
POINTS_RENDER_EXPORT struct points_data_source_t points_environment_data_source_get(struct points_environment_data_source_t *env);
POINTS_RENDER_EXPORT void points_environment_data_source_set_ground_z(struct points_environment_data_source_t *env, double ground_z);

#ifdef __cplusplus
}
#endif
#endif //POINTS_ENVIRONMENT_DATA_SOURCE_H
