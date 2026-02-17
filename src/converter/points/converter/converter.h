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

#include <points/common/error.h>
#include <points/common/format.h>
#include <points/converter/export.h>

#ifdef __cplusplus
extern "C" {
#endif

namespace points
{
namespace converter
{
struct header_t
{
  uint64_t point_count;
  double offset[3];
  double scale[3];
  double min[3];
  double max[3];
};

POINTS_CONVERTER_EXPORT void attributes_add_attribute(struct attributes_t *attributes, const char *name, uint32_t name_size, enum type_t format, enum components_t components);

struct attribute_t
{
  attribute_t(const char *name, uint32_t name_size, enum type_t format, enum components_t components)
    : name(name)
    , name_size(name_size)
    , type(format)
    , components(components)
  {
  }

  const char *name;
  uint32_t name_size;
  type_t type;
  components_t components;
};

struct buffer_t
{
  buffer_t()
    : data(nullptr)
    , size(0)
  {
  }

  buffer_t(void *data, uint32_t size)
    : data(data)
    , size(size)
  {
  }

  void *data;
  uint32_t size;
};

struct converter_file_pre_init_info_t
{
  double aabb_min[3];
  uint64_t approximate_point_count;
  uint8_t found_aabb_min;
  uint8_t approximate_point_size_bytes;
  bool found_point_count;
};

typedef converter_file_pre_init_info_t (*converter_file_pre_init_callback_t)(const char *filename, size_t filename_size, struct error_t **error);

typedef void (*converter_file_init_callback_t)(const char *filename, size_t filename_size, header_t *header, attributes_t *attributes, void **user_ptr, struct error_t **error);

typedef void (*converter_file_convert_data_callback_t)(void *user_ptr, const header_t *header, const attribute_t *attributes, uint32_t attributes_size, uint32_t max_points_to_convert, buffer_t *buffers,
                                                       uint32_t buffers_size, uint32_t *points_read, uint8_t *done, struct error_t **error);

typedef void (*converter_file_destroy_user_ptr_t)(void *user_ptr);

struct converter_file_convert_callbacks_t
{
  converter_file_pre_init_callback_t pre_init;
  converter_file_init_callback_t init;
  converter_file_convert_data_callback_t convert_data;
  converter_file_destroy_user_ptr_t destroy_user_ptr;
};

typedef void (*converter_progress_callback_t)(void *user_ptr, float progress);

typedef void (*converter_warning_callback_t)(void *user_ptr, const char *message);

typedef void (*converter_error_callback_t)(void *user_ptr, const struct error_t *error);

typedef void (*converter_done_callback_t)(void *user_ptr);

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

enum converter_open_file_semantics_t
{
  open_file_semantics_open_existing,
  open_file_semantics_truncate
};

struct converter_t;
POINTS_CONVERTER_EXPORT struct converter_t *converter_create(const char *cache_filename, uint64_t cache_filename_size, enum converter_open_file_semantics_t open_file_semantics, struct error_t **error);

POINTS_CONVERTER_EXPORT void converter_destroy(converter_t *destroy);

POINTS_CONVERTER_EXPORT void converter_set_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks);

POINTS_CONVERTER_EXPORT void converter_set_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks, void *user_ptr);

POINTS_CONVERTER_EXPORT void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count);

POINTS_CONVERTER_EXPORT void converter_wait_idle(converter_t *converter);

POINTS_CONVERTER_EXPORT converter_conversion_status_t converter_status(converter_t *converter);
} // namespace converter
} // namespace points
#ifdef __cplusplus
}
#endif

#endif
