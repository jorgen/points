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

#include <stdint.h>

#include <points/converter/export.h>

#include <points/converter/converter.h>
#include <points/render/data_source.h>
#include <points/render/renderer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct points_converter_data_source_t;
POINTS_CONVERTER_EXPORT struct points_converter_data_source_t *points_converter_data_source_create(const char *url, uint32_t url_len, struct points_error_t *error, struct points_renderer_t *renderer);

// As points_converter_data_source_create, but first applies `connection` (a vendor connection string --
// credentials / endpoint / region; same grammar/keys as points_converter_create_with_connection, see vio
// connection_string.h) for the dataset URL's provider, so a dataset can be streamed directly from a cloud
// store (s3://bucket/prefix, az://container/prefix). `connection` may be null/empty for local files
// (file://, bare paths) or when credentials come from the AWS_*/AZURE_* environment; for a public bucket
// pass "anonymous=true" (optionally with "region=..."). A no-op for local/dir/mem URLs.
POINTS_CONVERTER_EXPORT struct points_converter_data_source_t *points_converter_data_source_create_with_connection(const char *url, uint32_t url_len, const char *connection, uint32_t connection_len, struct points_error_t *error, struct points_renderer_t *renderer);

POINTS_CONVERTER_EXPORT void points_converter_data_source_destroy(struct points_converter_data_source_t *converter_data_source);
POINTS_CONVERTER_EXPORT struct points_data_source_t points_converter_data_source_get(struct points_converter_data_source_t *converter_data_source);

typedef void (*points_converter_data_source_request_aabb_callback_t)(double aabb_min[3], double aabb_max[3], void *user_ptr);
POINTS_CONVERTER_EXPORT void points_converter_data_source_request_aabb(struct points_converter_data_source_t *converter_data_source, points_converter_data_source_request_aabb_callback_t callback, void *user_ptr);

POINTS_CONVERTER_EXPORT uint32_t points_converter_data_attribute_count(struct points_converter_data_source_t *converter_data_source);
POINTS_CONVERTER_EXPORT uint32_t points_converter_data_get_attribute_name(struct points_converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_buffer_size);

POINTS_CONVERTER_EXPORT void points_converter_data_set_rendered_attribute(struct points_converter_data_source_t *converter_data_source, const char *name, uint32_t name_len);

POINTS_CONVERTER_EXPORT void points_converter_data_source_set_viewport(struct points_converter_data_source_t *converter_data_source, int width, int height);
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_pixel_error_threshold(struct points_converter_data_source_t *converter_data_source, double threshold);
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_render_density_px(struct points_converter_data_source_t *converter_data_source, double density_px);
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_gpu_memory_budget(struct points_converter_data_source_t *converter_data_source, size_t budget_bytes);
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_upload_budget_per_frame(struct points_converter_data_source_t *converter_data_source, size_t budget_bytes);
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_max_in_flight_io(struct points_converter_data_source_t *converter_data_source, int max_requests);

POINTS_CONVERTER_EXPORT uint64_t points_converter_data_source_get_points_rendered(struct points_converter_data_source_t *converter_data_source);

POINTS_CONVERTER_EXPORT uint8_t points_converter_data_source_is_animating(struct points_converter_data_source_t *converter_data_source);

POINTS_CONVERTER_EXPORT void points_converter_data_source_get_frame_timings(struct points_converter_data_source_t *converter_data_source,
  double *tree_walk_ms, double *buffer_reconciliation_ms, double *gpu_upload_ms,
  double *refine_strategy_ms, double *frontier_scheduling_ms,
  double *draw_emission_ms, double *eviction_ms, double *total_ms,
  int *registry_node_count, int *active_set_size, int *nodes_drawn,
  int *transitioning_count, int *nodes_evicted, int *nodes_reconcile_destroyed,
  int *walker_node_count, uint64_t *walker_total_points, int *walker_trees_to_load,
  int *io_in_flight);

POINTS_CONVERTER_EXPORT void points_converter_data_source_set_debug_transitions(struct points_converter_data_source_t *cds, uint8_t enabled);

/* Virtual subnodes (render-time balanced LOD for spanning leaves). Toggling off tears every virtual cut down
 * and falls the leaves back to their monoliths, so it is a live A/B on one camera path. */
POINTS_CONVERTER_EXPORT void points_converter_data_source_set_enable_virtual_subtrees(struct points_converter_data_source_t *cds, uint8_t enabled);
POINTS_CONVERTER_EXPORT uint8_t points_converter_data_source_get_enable_virtual_subtrees(struct points_converter_data_source_t *cds);
/* Observability: how many spanning leaves are currently promoted, the GPU bytes their virtual nodes hold, the
 * CPU bytes their resident sources pin, and how many virtual nodes were drawn last frame. */
POINTS_CONVERTER_EXPORT void points_converter_data_source_get_virtual_stats(struct points_converter_data_source_t *cds,
  uint32_t *promoted, uint64_t *gpu_bytes, uint64_t *resident_cpu_bytes, uint32_t *nodes_drawn);

POINTS_CONVERTER_EXPORT void points_converter_data_source_set_show_bounding_boxes(struct points_converter_data_source_t *cds, uint8_t enabled);
POINTS_CONVERTER_EXPORT struct points_data_source_t points_converter_data_source_get_bbox_data_source(struct points_converter_data_source_t *cds);

POINTS_CONVERTER_EXPORT void points_converter_data_source_get_tight_aabb(struct points_converter_data_source_t *cds, double min[3], double max[3]);

#ifdef __cplusplus
}
#endif
#endif // POINTS_CONVERTER_DATA_SOURCE_H
