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

#include <stdbool.h>
#include <stdint.h>

#include <points/common/error.h>
#include <points/common/format.h>
#include <points/converter/export.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_converter_header_t
{
  uint64_t point_count;
  double offset[3];
  double scale[3];
  double min[3];
  double max[3];
};

struct points_converter_attributes_t;
POINTS_CONVERTER_EXPORT void points_converter_attributes_add_attribute(struct points_converter_attributes_t *attributes, const char *name, uint32_t name_size, enum points_type_t format, enum points_components_t components);

struct points_converter_attribute_t
{
#ifdef __cplusplus
  points_converter_attribute_t(const char *a_name, uint32_t a_name_size, enum points_type_t format, enum points_components_t a_components)
    : name(a_name)
    , name_size(a_name_size)
    , type(format)
    , components(a_components)
  {
  }
#endif

  const char *name;
  uint32_t name_size;
  enum points_type_t type;
  enum points_components_t components;
};

struct points_converter_buffer_t
{
#ifdef __cplusplus
  points_converter_buffer_t()
    : data(nullptr)
    , size(0)
  {
  }

  points_converter_buffer_t(void *a_data, uint32_t a_size)
    : data(a_data)
    , size(a_size)
  {
  }
#endif

  void *data;
  uint32_t size;
};

struct points_converter_file_pre_init_info_t
{
  double aabb_min[3];
  uint64_t approximate_point_count;
  uint64_t input_file_size_bytes;
  uint8_t found_aabb_min;
  uint8_t approximate_point_size_bytes;
  uint8_t found_point_count;
};

typedef struct points_converter_file_pre_init_info_t (*points_converter_file_pre_init_callback_t)(const char *filename, size_t filename_size, struct points_error_t **error);

typedef void (*points_converter_file_init_callback_t)(const char *filename, size_t filename_size, struct points_converter_header_t *header, struct points_converter_attributes_t *attributes, void **user_ptr, struct points_error_t **error);

typedef void (*points_converter_file_convert_data_callback_t)(void *user_ptr, const struct points_converter_header_t *header, const struct points_converter_attribute_t *attributes, uint32_t attributes_size, uint32_t max_points_to_convert, struct points_converter_buffer_t *buffers,
                                                       uint32_t buffers_size, uint32_t *points_read, uint8_t *done, struct points_error_t **error);

typedef void (*points_converter_file_destroy_user_ptr_t)(void *user_ptr);

struct points_converter_file_convert_callbacks_t
{
  points_converter_file_pre_init_callback_t pre_init;
  points_converter_file_init_callback_t init;
  points_converter_file_convert_data_callback_t convert_data;
  points_converter_file_destroy_user_ptr_t destroy_user_ptr;
};

typedef void (*points_converter_progress_callback_t)(void *user_ptr, float progress);

typedef void (*points_converter_warning_callback_t)(void *user_ptr, const char *message);

typedef void (*points_converter_error_callback_t)(void *user_ptr, const struct points_error_t *error);

typedef void (*points_converter_done_callback_t)(void *user_ptr);

struct points_converter_runtime_callbacks_t
{
  points_converter_progress_callback_t progress;
  points_converter_warning_callback_t warning;
  points_converter_error_callback_t error;
  points_converter_done_callback_t done;
};

struct points_converter_buffer_callbacks_t
{
  int tmp;
};

struct points_converter_str_buffer
{
  const char *data;
  uint32_t size;
};

enum points_converter_conversion_status_t
{
  points_conversion_status_error,
  points_conversion_status_in_progress,
  points_conversion_status_completed
};

enum points_converter_open_file_semantics_t
{
  points_open_file_semantics_open_existing,
  points_open_file_semantics_truncate,
  points_open_file_semantics_read_only
};

enum points_converter_compression_t
{
  points_converter_compression_none = 0,
  points_converter_compression_blosc2 = 1,
  points_converter_compression_zstd = 2,
  points_converter_compression_huff0 = 3
};

struct points_converter_attribute_stats_t
{
  char name[64];
  enum points_type_t type;
  enum points_components_t components;
  uint64_t buffer_count;
  uint64_t uncompressed_bytes;
  uint64_t compressed_bytes;
  double min_value;  double max_value;
  uint64_t path_counts[4];
  uint64_t lod_buffer_count;
  uint64_t lod_uncompressed_bytes;
  uint64_t lod_compressed_bytes;
};

struct points_converter_stats_t
{
  uint32_t input_file_count;
  uint32_t total_buffer_count;
  uint32_t lod_buffer_count;
  uint32_t compression_method;
  uint64_t input_file_size_bytes;
  uint32_t attribute_count;
  struct points_converter_attribute_stats_t attributes[32];
};

struct points_converter_io_stats_t
{
  uint64_t total_bytes;
  uint64_t total_time_us;
  uint32_t operation_count;
  double avg_mbps;
  double peak_mbps;
  double low_mbps;
};

struct points_converter_perf_stats_t
{
  double total_time_seconds;
  double total_bytes_written_mb;
  double overall_mbps;
  struct points_converter_io_stats_t source_read;
  struct points_converter_io_stats_t sort;
  struct points_converter_io_stats_t source_write;
  struct points_converter_io_stats_t lod_read;
  struct points_converter_io_stats_t lod_write;
  double tree_build_seconds;
  double lod_generation_seconds;
  uint64_t cache_hits;
  uint64_t cache_misses;
};

struct points_converter_t;
POINTS_CONVERTER_EXPORT struct points_converter_t *points_converter_create(const char *cache_filename, uint64_t cache_filename_size, enum points_converter_open_file_semantics_t open_file_semantics, struct points_error_t **error);

POINTS_CONVERTER_EXPORT void points_converter_destroy(struct points_converter_t *destroy);

POINTS_CONVERTER_EXPORT void points_converter_set_file_converter_callbacks(struct points_converter_t *converter, struct points_converter_file_convert_callbacks_t callbacks);

POINTS_CONVERTER_EXPORT void points_converter_set_runtime_callbacks(struct points_converter_t *converter, struct points_converter_runtime_callbacks_t callbacks, void *user_ptr);

POINTS_CONVERTER_EXPORT void points_converter_set_compression(struct points_converter_t *converter, enum points_converter_compression_t compression);

POINTS_CONVERTER_EXPORT void points_converter_add_data_file(struct points_converter_t *converter, struct points_converter_str_buffer *buffers, uint32_t buffer_count);

POINTS_CONVERTER_EXPORT void points_converter_wait_idle(struct points_converter_t *converter);

POINTS_CONVERTER_EXPORT enum points_converter_conversion_status_t points_converter_status(struct points_converter_t *converter);

POINTS_CONVERTER_EXPORT bool points_converter_get_compression_stats(struct points_converter_t *converter, struct points_converter_stats_t *stats);

POINTS_CONVERTER_EXPORT bool points_converter_get_perf_stats(struct points_converter_t *converter, struct points_converter_perf_stats_t *perf_stats);

POINTS_CONVERTER_EXPORT bool points_converter_get_live_perf_stats(struct points_converter_t *converter, struct points_converter_perf_stats_t *perf_stats);

#ifdef __cplusplus
}
#endif

#endif
