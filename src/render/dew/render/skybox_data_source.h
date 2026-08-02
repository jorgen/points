/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#ifndef DEW_SKYBOX_DATA_SOURCE_H
#define DEW_SKYBOX_DATA_SOURCE_H

#include <dew/render/export.h>
#include <dew/render/data_source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_skybox_data_t
{
  void *positive_x;
  void *negative_x;
  void *positive_y;
  void *negative_y;
  void *positive_z;
  void *negative_z;
  int positive_x_size;
  int negative_x_size;
  int positive_y_size;
  int negative_y_size;
  int positive_z_size;
  int negative_z_size;
};

struct dew_skybox_data_source_t;
DEW_RENDER_EXPORT struct dew_skybox_data_source_t *dew_skybox_data_source_create(struct dew_renderer_t *renderer, struct dew_skybox_data_t data);
DEW_RENDER_EXPORT void dew_skybox_data_source_destroy(struct dew_skybox_data_source_t *skybox_data_source);
DEW_RENDER_EXPORT struct dew_data_source_t dew_skybox_data_source_get(struct dew_skybox_data_source_t *skybox_data_source);

#ifdef __cplusplus
}
#endif
#endif //DEW_SKYBOX_DATA_SOURCE_H
