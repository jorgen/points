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
  attribute_t(const char *a_name, uint32_t a_name_size, enum type_t format, enum components_t a_components)
    : name(a_name)
    , name_size(a_name_size)
    , type(format)
    , components(a_components)
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

  buffer_t(void *a_data, uint32_t a_size)
    : data(a_data)
    , size(a_size)
  {
  }

  void *data;
  uint32_t size;
};

struct converter_file_pre_init_info_t
{
  double aabb_min[3];
  uint64_t approximate_point_count;
  uint64_t input_file_size_bytes;
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
  open_file_semantics_truncate,
  open_file_semantics_read_only
};

enum converter_compression_t
{
  converter_compression_none = 0,
  converter_compression_blosc2 = 1,
  converter_compression_zstd = 2,
  converter_compression_huff0 = 3
};

struct converter_attribute_stats_t
{
  char name[64];
  enum type_t type;
  enum components_t components;
  uint64_t buffer_count;
  uint64_t uncompressed_bytes;
  uint64_t compressed_bytes;
  double min_value;  double max_value;
  uint64_t path_counts[4]; // [0]=raw, [1]=decorrelated, [2]=component_delta, [3]=decorrelated+component_delta
  uint64_t lod_buffer_count;
  uint64_t lod_uncompressed_bytes;
  uint64_t lod_compressed_bytes;
};

struct converter_stats_t
{
  uint32_t input_file_count;
  uint32_t total_buffer_count;
  uint32_t lod_buffer_count;
  uint32_t compression_method;
  uint64_t input_file_size_bytes;
  uint32_t attribute_count;
  struct converter_attribute_stats_t attributes[32];
};

struct converter_io_stats_t
{
  uint64_t total_bytes;
  uint64_t total_time_us;
  uint32_t operation_count;
  double avg_mbps;
  double peak_mbps;
  double low_mbps;
};

struct converter_perf_stats_t
{
  double total_time_seconds;
  double total_bytes_written_mb;
  double overall_mbps;
  struct converter_io_stats_t source_read;
  struct converter_io_stats_t sort;
  struct converter_io_stats_t source_write;
  struct converter_io_stats_t lod_read;
  struct converter_io_stats_t lod_write;
  double tree_build_seconds;
  double lod_generation_seconds;
  uint64_t cache_hits;
  uint64_t cache_misses;
};

struct converter_t;
POINTS_CONVERTER_EXPORT struct converter_t *converter_create(const char *cache_filename, uint64_t cache_filename_size, enum converter_open_file_semantics_t open_file_semantics, struct error_t **error);

POINTS_CONVERTER_EXPORT void converter_destroy(converter_t *destroy);

POINTS_CONVERTER_EXPORT void converter_set_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks);

POINTS_CONVERTER_EXPORT void converter_set_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks, void *user_ptr);

POINTS_CONVERTER_EXPORT void converter_set_compression(converter_t *converter, enum converter_compression_t compression);

POINTS_CONVERTER_EXPORT void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count);

POINTS_CONVERTER_EXPORT void converter_wait_idle(converter_t *converter);

POINTS_CONVERTER_EXPORT converter_conversion_status_t converter_status(converter_t *converter);

POINTS_CONVERTER_EXPORT void converter_get_compression_stats(struct converter_t *converter, struct converter_stats_t *stats);

POINTS_CONVERTER_EXPORT void converter_get_perf_stats(struct converter_t *converter, struct converter_perf_stats_t *perf_stats);

POINTS_CONVERTER_EXPORT void converter_get_live_perf_stats(struct converter_t *converter, struct converter_perf_stats_t *perf_stats);
} // namespace converter
} // namespace points
#ifdef __cplusplus
}
#endif

#endif
