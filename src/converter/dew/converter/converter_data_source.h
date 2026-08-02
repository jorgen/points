/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#ifndef DEW_CONVERTER_DATA_SOURCE_H
#define DEW_CONVERTER_DATA_SOURCE_H

#include <stdint.h>

#include <dew/converter/export.h>

#include <dew/converter/converter.h>
#include <dew/render/data_source.h>
#include <dew/render/renderer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dew_converter_data_source_t;
DEW_CONVERTER_EXPORT struct dew_converter_data_source_t *dew_converter_data_source_create(const char *url, uint32_t url_len, struct dew_error_t *error, struct dew_renderer_t *renderer);

// As dew_converter_data_source_create, but first applies `connection` (a vendor connection string --
// credentials / endpoint / region; same grammar/keys as dew_converter_create_with_connection, see vio
// connection_string.h) for the dataset URL's provider, so a dataset can be streamed directly from a cloud
// store (s3://bucket/prefix, az://container/prefix). `connection` may be null/empty for local files
// (file://, bare paths) or when credentials come from the AWS_*/AZURE_* environment; for a public bucket
// pass "anonymous=true" (optionally with "region=..."). A no-op for local/dir/mem URLs.
DEW_CONVERTER_EXPORT struct dew_converter_data_source_t *dew_converter_data_source_create_with_connection(const char *url, uint32_t url_len, const char *connection, uint32_t connection_len, struct dew_error_t *error, struct dew_renderer_t *renderer);

DEW_CONVERTER_EXPORT void dew_converter_data_source_destroy(struct dew_converter_data_source_t *converter_data_source);
DEW_CONVERTER_EXPORT struct dew_data_source_t dew_converter_data_source_get(struct dew_converter_data_source_t *converter_data_source);

typedef void (*dew_converter_data_source_request_aabb_callback_t)(double aabb_min[3], double aabb_max[3], void *user_ptr);
DEW_CONVERTER_EXPORT void dew_converter_data_source_request_aabb(struct dew_converter_data_source_t *converter_data_source, dew_converter_data_source_request_aabb_callback_t callback, void *user_ptr);

DEW_CONVERTER_EXPORT uint32_t dew_converter_data_attribute_count(struct dew_converter_data_source_t *converter_data_source);
DEW_CONVERTER_EXPORT uint32_t dew_converter_data_get_attribute_name(struct dew_converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_buffer_size);

DEW_CONVERTER_EXPORT void dew_converter_data_set_rendered_attribute(struct dew_converter_data_source_t *converter_data_source, const char *name, uint32_t name_len);

DEW_CONVERTER_EXPORT void dew_converter_data_source_set_viewport(struct dew_converter_data_source_t *converter_data_source, int width, int height);
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_pixel_error_threshold(struct dew_converter_data_source_t *converter_data_source, double threshold);
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_render_density_px(struct dew_converter_data_source_t *converter_data_source, double density_px);
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_gpu_memory_budget(struct dew_converter_data_source_t *converter_data_source, size_t budget_bytes);
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_upload_budget_per_frame(struct dew_converter_data_source_t *converter_data_source, size_t budget_bytes);
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_max_in_flight_io(struct dew_converter_data_source_t *converter_data_source, int max_requests);

/* One total CPU-memory budget for the streaming renderer. Internally derived into the read-cache size, the
 * decoded-backlog byte cap (new IO is refused while estimated in-flight + decoded-awaiting-upload bytes
 * exceed it), the virtual-subtree CPU-resident budget, and a clamp on max_in_flight_io (see
 * memory_budget.hpp for the formula). GPU memory has its own budget
 * (dew_converter_data_source_set_gpu_memory_budget) -- GL buffers live outside this heap. The default
 * (1GB) reproduces the historical sub-budget defaults. Values below 64MB are clamped up to 64MB. */
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_memory_budget(struct dew_converter_data_source_t *cds, uint64_t total_bytes);
DEW_CONVERTER_EXPORT uint64_t dew_converter_data_source_get_memory_budget(struct dew_converter_data_source_t *cds);

/* Observability for the memory budget and the wasm heap-pressure brake. heap_bytes/heap_max are the wasm
 * heap size and its link-time ceiling as probed on the last rendered frame (0/0 on native); backlog_bytes
 * is the estimated CPU held by in-flight + decoded-awaiting-upload nodes last frame; brake_level is
 * 0 none / 1 high (>=80% of ceiling) / 2 critical (>=90%). Any out-pointer may be null. */
DEW_CONVERTER_EXPORT void dew_converter_data_source_get_memory_stats(struct dew_converter_data_source_t *cds,
  uint64_t *heap_bytes, uint64_t *heap_max, uint64_t *budget_bytes, uint64_t *backlog_bytes,
  uint64_t *read_cache_bytes, uint64_t *resident_cpu_bytes, uint32_t *brake_level);

DEW_CONVERTER_EXPORT uint64_t dew_converter_data_source_get_points_rendered(struct dew_converter_data_source_t *converter_data_source);

DEW_CONVERTER_EXPORT uint8_t dew_converter_data_source_is_animating(struct dew_converter_data_source_t *converter_data_source);

DEW_CONVERTER_EXPORT void dew_converter_data_source_get_frame_timings(struct dew_converter_data_source_t *converter_data_source,
  double *tree_walk_ms, double *buffer_reconciliation_ms, double *gpu_upload_ms,
  double *refine_strategy_ms, double *frontier_scheduling_ms,
  double *draw_emission_ms, double *eviction_ms, double *total_ms,
  int *registry_node_count, int *active_set_size, int *nodes_drawn,
  int *transitioning_count, int *nodes_evicted, int *nodes_reconcile_destroyed,
  int *walker_node_count, uint64_t *walker_total_points, int *walker_trees_to_load,
  int *io_in_flight);

DEW_CONVERTER_EXPORT void dew_converter_data_source_set_debug_transitions(struct dew_converter_data_source_t *cds, uint8_t enabled);

/* Virtual subnodes (render-time balanced LOD for spanning leaves). Toggling off tears every virtual cut down
 * and falls the leaves back to their monoliths, so it is a live A/B on one camera path. */
DEW_CONVERTER_EXPORT void dew_converter_data_source_set_enable_virtual_subtrees(struct dew_converter_data_source_t *cds, uint8_t enabled);
DEW_CONVERTER_EXPORT uint8_t dew_converter_data_source_get_enable_virtual_subtrees(struct dew_converter_data_source_t *cds);
/* Observability: how many spanning leaves are currently promoted, the GPU bytes their virtual nodes hold, the
 * CPU bytes their resident sources pin, and how many virtual nodes were drawn last frame. */
DEW_CONVERTER_EXPORT void dew_converter_data_source_get_virtual_stats(struct dew_converter_data_source_t *cds,
  uint32_t *promoted, uint64_t *gpu_bytes, uint64_t *resident_cpu_bytes, uint32_t *nodes_drawn);

DEW_CONVERTER_EXPORT void dew_converter_data_source_set_show_bounding_boxes(struct dew_converter_data_source_t *cds, uint8_t enabled);
DEW_CONVERTER_EXPORT struct dew_data_source_t dew_converter_data_source_get_bbox_data_source(struct dew_converter_data_source_t *cds);

DEW_CONVERTER_EXPORT void dew_converter_data_source_get_tight_aabb(struct dew_converter_data_source_t *cds, double min[3], double max[3]);

#ifdef __cplusplus
}
#endif
#endif // DEW_CONVERTER_DATA_SOURCE_H
