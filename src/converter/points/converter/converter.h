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
#ifndef POINTS_CONVERTER_H
#define POINTS_CONVERTER_H

#include <stdint.h>

#include <points/converter/export.h>
#include <points/converter/error.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{

enum format_t
{
  format_u8,
  format_i8,
  format_u16,
  format_i16,
  format_u32,
  format_i32,
  format_u64,
  format_i64,
  format_r32,
  format_r64
};

enum components_t
{
  components_1 = 1,
  components_2 = 2,
  components_3 = 3,
  components_4 = 4
};

struct header_t
{
  uint64_t point_count;
  double offset[3];
  double scale[3];
  double min[3];
  double max[3];
};
POINTS_CONVERTER_EXPORT void header_set_name(header_t *header, const char *name, uint64_t name_size);
POINTS_CONVERTER_EXPORT const char *header_get_name(header_t *header);
POINTS_CONVERTER_EXPORT void header_add_attribute(struct header_t *header, const char *name, uint64_t name_size, enum format_t format, enum components_t components, int group = 0);

struct attribute_t
{
  const char *name;
  uint64_t name_size;
  format_t format;
  components_t components;
  int group;
};

struct buffer_t
{
  void *data;
  uint64_t size;
};

typedef void (*converter_file_init_callback_t)(const char *filename, size_t filename_size, header_t *header, void **user_ptr, struct error_t **error);
typedef uint64_t (*converter_file_convert_data_callback_t)(void *user_ptr, const header_t *header, const attribute_t *attributes, uint64_t attributes_size, buffer_t *buffers, uint64_t buffers_size, uint64_t max_points_to_convert, uint8_t *done, struct error_t **error);
typedef void (*converter_file_destroy_user_ptr_t)(void *user_ptr);

struct converter_file_convert_callbacks_t
{
  converter_file_init_callback_t init;
  converter_file_convert_data_callback_t convert_data;
  converter_file_destroy_user_ptr_t destroy_user_ptr;
};

typedef void (*converter_progress_callback_t)(float progress);
typedef void (*converter_warning_callback_t)(const char *message);
typedef void (*converter_error_callback_t)(const struct error_t *error);
typedef void (*converter_done_callback_t)();

struct converter_runtime_callbacks_t
{
  converter_progress_callback_t progress;
  converter_warning_callback_t warning;
  converter_error_callback_t error;
  converter_done_callback_t done;
};


struct converter_buffer_callbacks_t
{
 int tmp;
};

struct str_buffer
{
  const char *data;
  uint32_t size;
};

enum converter_conversion_status_t
{
  conversion_status_error, 
  conversion_status_in_progress,
  conversion_status_completed
};

struct converter_t;
POINTS_CONVERTER_EXPORT struct converter_t *converter_create(const char *cache_filename, uint64_t cache_filename_size);
POINTS_CONVERTER_EXPORT void converter_destroy(converter_t *destroy);
POINTS_CONVERTER_EXPORT void converter_add_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks);
POINTS_CONVERTER_EXPORT void converter_add_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks);
POINTS_CONVERTER_EXPORT void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count);
POINTS_CONVERTER_EXPORT void converter_wait_finish(converter_t *converter);
POINTS_CONVERTER_EXPORT converter_conversion_status_t converter_status(converter_t *converter, error_t **errors, size_t *error_count);

}

} // namespace points
#ifdef __cplusplus
}
#endif

#endif
