/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#ifndef POINTS_CONVERTER_DATA_SOURCE_H
#define POINTS_CONVERTER_DATA_SOURCE_H

#include <points/converter/export.h>

#include <points/converter/converter.h>
#include <points/render/data_source.h>
#include <points/render/renderer.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{
struct converter_data_source_t;
POINTS_CONVERTER_EXPORT struct converter_data_source_t *converter_data_source_create(const char *url, uint32_t url_len, error_t *error, struct render::renderer_t *renderer);
POINTS_CONVERTER_EXPORT void converter_data_source_destroy(struct converter_data_source_t *converter_data_source);
POINTS_CONVERTER_EXPORT struct render::data_source_t converter_data_source_get(struct converter_data_source_t *converter_data_source);

typedef void (*converter_data_source_request_aabb_callback_t)(double aabb_min[3], double aabb_max[3], void *user_ptr);
POINTS_CONVERTER_EXPORT void converter_data_source_request_aabb(struct converter_data_source_t *converter_data_source, converter_data_source_request_aabb_callback_t callback, void *user_ptr);

POINTS_CONVERTER_EXPORT uint32_t converter_data_attribute_count(struct converter_data_source_t *converter_data_source);
POINTS_CONVERTER_EXPORT uint32_t converter_data_get_attribute_name(struct converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_buffer_size);

POINTS_CONVERTER_EXPORT void converter_data_set_rendered_attribute(struct converter_data_source_t *converter_data_source, const char *name, uint32_t name_len);

} // namespace converter
} // namespace points

#ifdef __cplusplus
}
#endif
#endif // POINTS_CONVERTER_DATA_SOURCE_H
