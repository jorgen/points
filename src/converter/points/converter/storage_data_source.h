/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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
#ifndef STORAGE_DATA_SOURCE_H
#define STORAGE_DATA_SOURCE_H

#include <points/converter/export.h>

#include <points/render/renderer.h>
#include <points/render/data_source.h>
#include <points/converter/converter.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{
struct storage_data_source_t;
POINTS_CONVERTER_EXPORT struct storage_data_source_t *storage_data_source_create(struct converter::converter_t *converter, struct render::renderer_t *renderer);
POINTS_CONVERTER_EXPORT void storage_data_source_destroy(struct storage_data_source_t *storage_data_source);
POINTS_CONVERTER_EXPORT struct render::data_source_t storage_data_source_get(struct storage_data_source_t *storage_data_source);
POINTS_CONVERTER_EXPORT int storage_data_source_ids(struct storage_data_source_t *storage_data_source, uint32_t **id_buffer, uint32_t **sub_id_buffer, int buffer_size);
POINTS_CONVERTER_EXPORT int storage_data_source_ids_count(struct storage_data_source_t *storage_data_source);
POINTS_CONVERTER_EXPORT void storage_data_source_render(struct storage_data_source_t *storage_data_source, uint32_t id_buffer, uint32_t sub_id_buffer);
}

} // namespace points

#ifdef __cplusplus
}
#endif
#endif // STORAGE_DATA_SOURCE_H
