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

/* Destination-upload callbacks. A SEPARATE struct (not an extension of runtime_callbacks_t, which
 * is passed by value -- growing it would silently break the ABI of every existing caller). All
 * callbacks fire on internal threads; keep them cheap and thread-safe. */
typedef void (*points_converter_upload_progress_callback_t)(void *user_ptr, uint64_t bytes_uploaded, uint32_t bands_committed);
/* A band of finalized subtrees committed at the destination; everything strictly below done_morton
 * (a 192-bit morton code, 3x64 little-endian) is durably readable there. */
typedef void (*points_converter_band_committed_callback_t)(void *user_ptr, uint32_t band_id, const uint64_t done_morton[3]);
typedef void (*points_converter_upload_error_callback_t)(void *user_ptr, const struct points_error_t *error, uint8_t parked);
typedef void (*points_converter_upload_done_callback_t)(void *user_ptr);

struct points_converter_upload_callbacks_t
{
  points_converter_upload_progress_callback_t progress;
  points_converter_band_committed_callback_t band_committed;
  points_converter_upload_error_callback_t error;
  points_converter_upload_done_callback_t done;
};

struct points_converter_upload_state_t
{
  uint64_t bytes_uploaded;
  uint32_t bands_committed;
  uint32_t objects_written;
  uint8_t upload_parked;      /* retries exhausted; resume by reopening later */
  uint8_t destination_complete;
  uint64_t cache_resident_bytes;
  uint64_t cache_max_bytes;
  uint64_t cache_spilled_bytes;
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

// As points_converter_create, but first applies `connection` (a vendor connection string -- credentials /
// endpoint / region; see connection_cli.h + vio connection_string.h) for the output URL's provider, so a
// dataset can be written directly to a cloud store. `connection` may be null/empty for local outputs or
// when credentials come from the environment. `open_file_semantics` is typically truncate.
POINTS_CONVERTER_EXPORT struct points_converter_t *points_converter_create_with_connection(const char *url, uint64_t url_size, const char *connection, uint64_t connection_size, enum points_converter_open_file_semantics_t open_file_semantics, struct points_error_t **error);

// Destination mode: convert into a LOCAL cache file (`cache_path`, a bare path or file:// URL) while
// finalized subtrees upload incrementally to `destination_url` (s3:// az:// dir://; JLP2 layout).
// The cache stays a valid, renderable JLP at every checkpoint; blobs already uploaded are evicted
// from it under the cache cap (see points_converter_set_cache_max_bytes), and when the disk runs out
// mid-conversion, unfinished data spills to the destination's spill/ area and is fetched back on
// demand. A null/empty destination degrades to points_converter_create. `connection` carries the
// destination's credentials (null/empty = environment). Reopening the same cache+destination resumes:
// committed bands are never re-uploaded (a destination belonging to a different dataset generation is
// refused via the embedded uuid).
POINTS_CONVERTER_EXPORT struct points_converter_t *points_converter_create_with_destination(const char *cache_path, uint64_t cache_path_size, const char *destination_url, uint64_t destination_url_size, const char *connection, uint64_t connection_size,
                                                                                            enum points_converter_open_file_semantics_t open_file_semantics, struct points_error_t **error);

// Cache-file resident-bytes cap for destination mode. 0 = unlimited (the default). Callable any
// time; lowering it triggers eviction/spill passes.
POINTS_CONVERTER_EXPORT void points_converter_set_cache_max_bytes(struct points_converter_t *converter, uint64_t max_bytes);

// In-RAM decompressed read-cache budget (default 256MB). Also the seam a render/query consumer tunes.
POINTS_CONVERTER_EXPORT void points_converter_set_read_cache_bytes(struct points_converter_t *converter, uint64_t max_bytes);

POINTS_CONVERTER_EXPORT void points_converter_set_upload_callbacks(struct points_converter_t *converter, struct points_converter_upload_callbacks_t callbacks, void *user_ptr);

// Snapshot of upload/cache-tier state (approximate counters; safe any time). Returns false when the
// converter has no destination configured.
POINTS_CONVERTER_EXPORT bool points_converter_get_upload_state(struct points_converter_t *converter, struct points_converter_upload_state_t *state);

// Wait until conversion is complete and the cache file is a fully checkpointed, valid JLP --
// uploads may still be in flight (contrast points_converter_wait_idle, which also drains them).
POINTS_CONVERTER_EXPORT void points_converter_wait_local_complete(struct points_converter_t *converter);

POINTS_CONVERTER_EXPORT void points_converter_destroy(struct points_converter_t *destroy);

POINTS_CONVERTER_EXPORT void points_converter_set_file_converter_callbacks(struct points_converter_t *converter, struct points_converter_file_convert_callbacks_t callbacks);

POINTS_CONVERTER_EXPORT void points_converter_set_runtime_callbacks(struct points_converter_t *converter, struct points_converter_runtime_callbacks_t callbacks, void *user_ptr);

POINTS_CONVERTER_EXPORT void points_converter_set_compression(struct points_converter_t *converter, enum points_converter_compression_t compression);

POINTS_CONVERTER_EXPORT void points_converter_set_store_original_order(struct points_converter_t *converter, bool store);

POINTS_CONVERTER_EXPORT void points_converter_set_compression_level(struct points_converter_t *converter, int level);

// Set the target points per octree node. This doubles as the read/sort chunk size, so it is the main
// control over stored blob size (one node ~ one compressed blob per attribute). Default 200000. Must be
// called before points_converter_add_data_file.
POINTS_CONVERTER_EXPORT void points_converter_set_node_point_limit(struct points_converter_t *converter, uint32_t points);

// Read/sort chunk byte target (default 64 MiB): the converter ingests each input in chunks of about
// this many bytes (computed from the file's per-point width, never below the node point limit,
// capped at 8M points per chunk). Larger chunks amortize source reads and sorting; the octree still
// subdivides to node_point_limit leaves and finalized leaves are rewritten into per-node units.
// Must be called before points_converter_add_data_file.
POINTS_CONVERTER_EXPORT void points_converter_set_read_chunk_bytes(struct points_converter_t *converter, uint64_t bytes);

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
