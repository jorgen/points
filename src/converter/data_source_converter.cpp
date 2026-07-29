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
#include "data_source_converter.hpp"
#include "data_source.hpp"
#include "input_header.hpp"
#include "lod_quantize.hpp" // make_lod_random_offsets (same scheme as the converter)
#include "native_node_data_loader.hpp"
#include "virtual_tree.hpp" // build_resident_source, make_virtual_root, process_virtual_trees, emit_virtual_draws
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fmt/printf.h>

#include "renderer.hpp"

using namespace points;
using namespace points::converter;

points_converter_data_source_t::points_converter_data_source_t(const std::string &a_url, render::callback_manager_t &a_callbacks)
  : url(a_url)
  , processor(a_url, file_existence_requirement_t::exist, error)
  , callbacks(a_callbacks)
{
  if (error.code != 0)
  {
    return;
  }
  data_source.user_ptr = this;
  data_source.add_to_frame = [](points_frame_camera_t *camera, points_to_render_t *to_render, void *user_ptr)
  {
    auto *thiz = static_cast<points_converter_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };

  bbox_data_source = std::make_unique<node_bbox_data_source_t>(callbacks);

  node_loader = std::make_unique<native_node_data_loader_t>(processor.storage_handler());

  // Read compression stats for attribute normalization
  attribute_stats = processor.storage_handler().get_compression_stats();

  if (processor.attrib_name_registry_count() > 2)
  {
    char buffer[256];
    auto str_size = processor.attrib_name_registry_get(1, buffer, sizeof(buffer));
    next_attribute_name.assign(buffer, str_size);
  }

  last_frame_time = std::chrono::high_resolution_clock::now();
}

void points_converter_data_source_t::add_to_frame(points_frame_camera_t *c_camera, points_to_render_t *to_render)
{
  using clock = std::chrono::high_resolution_clock;
  auto t_start = clock::now();

  float delta_ms = std::chrono::duration<float, std::milli>(t_start - last_frame_time).count();
  last_frame_time = t_start;
  // Clamp to avoid large jumps (e.g., first frame or debugger pause)
  if (delta_ms > 1000.0f)
    delta_ms = 16.0f;

  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  bool new_attribute = false;
  double frac_threshold;
  size_t frame_upload_budget;
  int max_io_in_flight;
  int frame_viewport_height;
  double frame_render_density_px;
  {
    std::unique_lock<std::mutex> lock(mutex);
    new_attribute = current_attribute_name != next_attribute_name;
    current_attribute_name = next_attribute_name;
    frac_threshold = screen_fraction_threshold;
    frame_upload_budget = upload_budget_per_frame;
    max_io_in_flight = max_in_flight_io;
    frame_viewport_height = viewport_height;
    frame_render_density_px = render_density_px;
  }

  // Handle attribute change
  if (new_attribute)
  {
    if (debug_transitions)
      fmt::print(stderr, "[transition-debug] === ATTRIBUTE CHANGE to '{}' ===\n", current_attribute_name);
    handle_attribute_change(render_list, callbacks, node_loader.get());

    current_attr_min = 0.0;
    current_attr_max = 1.0;
    for (auto &attr : attribute_stats.per_attribute)
    {
      if (attr.name == current_attribute_name && attr.min_value <= attr.max_value)
      {
        current_attr_min = attr.min_value;
        current_attr_max = attr.max_value;
        break;
      }
    }
  }

  // Phase 1: Tree walk
  glm::dvec3 camera_position = glm::dvec3(camera.inverse_view[3]);
  lod_params_t lod_params;
  lod_params.camera_position = camera_position;
  lod_params.projection = camera.projection;
  lod_params.screen_fraction_threshold = frac_threshold;

  if (cached_walker_attribute_source != current_attribute_name)
  {
    cached_walker_attribute_names = {std::string("xyz"), current_attribute_name};
    cached_walker_attribute_source = current_attribute_name;
  }
  frustum_tree_walker_t walker(camera.view_projection, lod_params, cached_walker_attribute_names);
  walker.m_previously_subdivided = std::move(previously_subdivided);
  walker.m_debug = debug_transitions;
  processor.walk_tree(walker);
  previously_subdivided.clear();
  for (auto &[parent, child] : walker.m_new_nodes.parent_child_edges)
    previously_subdivided.insert(parent);
  auto &walker_subsets = walker.m_new_nodes.point_subsets;
  std::sort(walker_subsets.begin(), walker_subsets.end(), render_node_less_than);
  frame_timings.walker_node_count = int(walker_subsets.size());
  frame_timings.walker_trees_to_load = int(walker.m_trees_to_load.size());
  {
    uint64_t total_pts = 0;
    for (auto &s : walker_subsets)
      total_pts += s.point_count.data;
    frame_timings.walker_total_points = total_pts;
  }
  auto t_after_tree_walk = clock::now();

  // Phase 2: Build render list
  render_list = build_render_list(walker_subsets, std::move(render_list),
      fade_duration_ms, callbacks, node_loader.get());
  frame_timings.render_list_size = int(render_list.size());
  auto t_after_build = clock::now();

  // Phase 3: IO + upload (single pass for distances, completions, scheduling, upload)
  auto tree_config = processor.tree_config();
  auto io_stats = process_io_and_upload(render_list, camera_position, tree_config,
      callbacks, node_loader.get(), convert_pool, camera, max_io_in_flight, max_new_io_per_frame,
      frame_upload_budget, gpu_memory_budget, current_attr_min, current_attr_max, enable_virtual_subtrees);
  frame_timings.io_in_flight = io_stats.io_in_flight;
  frame_timings.scan_classify_ms = io_stats.scan_classify_ms;
  frame_timings.schedule_io_ms = io_stats.schedule_io_ms;
  frame_timings.normalize_ms = io_stats.normalize_ms;
  frame_timings.gpu_upload_ms = io_stats.gpu_upload_ms;
  frame_timings.uploads_this_frame = io_stats.uploads_done;
  auto t_after_io_upload = clock::now();

  // Phase 4: Update fades
  update_fades(render_list, delta_ms, fade_duration_ms);
  auto t_after_fade = clock::now();

  // Phase 4.5: Virtual subnodes. Promote uploaded spanning leaves (is_leaf + should_subdivide) whose morton
  // data was salvaged into resident_handler, then walk each virtual octree (materialize/upload/evict). A live
  // cut sets draw_suppressed so emit_draws skips the leaf's own monolith.
  if (enable_virtual_subtrees)
  {
    if (virtual_lod_random_offsets.empty())
      virtual_lod_random_offsets = make_lod_random_offsets();
    // Promote EVERY spanning leaf reached by the walker (not just should_subdivide ones): the whole point is
    // that a far/small spanning leaf is drawn full-res (the dense-patch inversion) and needs a coarse LOD.
    // A leaf is worth promoting only if it has a coarser representation to offer (maskWidth = lod_span-9 > 0);
    // compact leaves keep the cheap monolith. Cap promotions/frame so the one-time decodes don't hitch.
    int promotions_left = int(virtual_max_promotions_per_frame);
    for (auto &np : render_list)
    {
      auto &node = *np;
      if (node.is_virtual_source || !node.resident_handler || node.gpu_state != render_node_gpu_state::uploaded)
        continue;
      if (node.point_count <= virtual_min_points || !node.walker_data.is_leaf)
      {
        node.resident_handler.reset(); // too small / not a leaf -> never promotes; drop the salvaged CPU dup
        continue;
      }
      if (node.resident_handler->header.lod_span <= lod_quantize_full_detail_level)
      {
        node.resident_handler.reset(); // compact leaf: maskWidth(lod_span)==0, no coarser LOD to offer
        continue;
      }
      if (promotions_left <= 0)
        continue; // ramp the rest over subsequent frames
      --promotions_left;
      node.resident = build_resident_source(node.resident_handler, tree_config);
      node.resident_handler.reset(); // the resident took its own ref to the data_handler
      node.virtual_root = make_virtual_root(*node.resident, node.walker_data.tight_aabb, node.walker_data.aabb);
      node.is_virtual_source = true;
    }
    virtual_frame_t vf;
    vf.camera = &camera;
    vf.camera_position = camera_position;
    vf.tree_config = &tree_config;
    vf.lod_params = &lod_params;
    vf.callbacks = &callbacks;
    vf.convert_pool = &convert_pool;
    vf.lod_random_offsets = &virtual_lod_random_offsets;
    vf.gpu_memory_budget = gpu_memory_budget;
    vf.gpu_memory_used = &virtual_gpu_used;
    vf.real_gpu_used = io_stats.gpu_memory_used; // monolith GPU total this frame -> shared budget gate
    vf.virtual_min_points = virtual_min_points;
    vf.frame_index = ++virtual_frame_counter;
    vf.delta_ms = delta_ms;
    vf.fade_duration_ms = fade_duration_ms;
    vf.viewport_height = frame_viewport_height;
    vf.render_density_px = frame_render_density_px;
    vf.attr_min = current_attr_min;
    vf.attr_max = current_attr_max;
    process_virtual_trees(render_list, vf);
  }

  // Collect bounding boxes and tight AABB, count stats
  {
    std::vector<node_bbox_t> loose_boxes;
    std::vector<node_bbox_t> tight_boxes;
    int loading = 0, converting = 0, uploaded = 0, fading_in = 0, fading_out = 0;
    for (auto &np : render_list)
    {
      if (np->fade_state != render_node_fade_state::fade_out)
      {
        tight_aabb_accumulator.min = glm::min(tight_aabb_accumulator.min, np->walker_data.tight_aabb.min);
        tight_aabb_accumulator.max = glm::max(tight_aabb_accumulator.max, np->walker_data.tight_aabb.max);
      }
      if (show_bounding_boxes && np->fade_state != render_node_fade_state::fade_out)
      {
        loose_boxes.push_back({np->walker_data.aabb.min, np->walker_data.aabb.max});
        tight_boxes.push_back({np->walker_data.tight_aabb.min, np->walker_data.tight_aabb.max});
      }
      if (np->io_state == render_node_io_state::loading || np->io_state == render_node_io_state::loaded)
        loading++;
      if (np->io_state == render_node_io_state::converting)
        converting++;
      if (np->gpu_state == render_node_gpu_state::uploaded)
        uploaded++;
      if (np->fade_state == render_node_fade_state::fade_in)
        fading_in++;
      if (np->fade_state == render_node_fade_state::fade_out)
        fading_out++;
    }
    if (show_bounding_boxes)
      bbox_data_source->update_boxes(loose_boxes, tight_boxes);
    frame_timings.nodes_loading = loading;
    frame_timings.nodes_converting = converting;
    frame_timings.nodes_uploaded = uploaded;
    frame_timings.nodes_fading_in = fading_in;
    frame_timings.nodes_fading_out = fading_out;
  }

  // Phase 5: Emit draws
  uint64_t pts_rendered = 0;
  frame_timings.nodes_drawn = emit_draws(render_list, callbacks, camera, tree_config, to_render, fade_duration_ms, frame_viewport_height, frame_render_density_px, pts_rendered);
  if (enable_virtual_subtrees)
  {
    frame_timings.nodes_drawn += emit_virtual_draws(render_list, callbacks, camera, tree_config, to_render, virtual_frame_counter, frame_viewport_height, frame_render_density_px, fade_duration_ms, pts_rendered);
    // Report virtual-subnode activity only when it changes, so it's a verifiable signal without per-frame spam.
    int promoted = 0;
    for (auto &np : render_list)
      if (np->is_virtual_source)
        promoted++;
    if (promoted != last_virtual_promoted)
    {
      fmt::print(stderr, "[virtual] promoted spanning leaves = {} (gpu {} KB)\n", promoted, virtual_gpu_used / 1024);
      last_virtual_promoted = promoted;
    }
  }
  points_rendered_last_frame = pts_rendered;
  auto t_after_emit = clock::now();

  auto t_end = clock::now();

  auto to_ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
  frame_timings.tree_walk_ms = to_ms(t_after_tree_walk - t_start);
  frame_timings.build_render_list_ms = to_ms(t_after_build - t_after_tree_walk);
  // scan_classify_ms, schedule_io_ms, normalize_ms, gpu_upload_ms set from io_stats above
  frame_timings.fade_ms = to_ms(t_after_fade - t_after_io_upload);
  frame_timings.emit_ms = to_ms(t_after_emit - t_after_fade);
  frame_timings.total_ms = to_ms(t_end - t_start);
}

struct points_converter_data_source_t *points_converter_data_source_create(const char *url, uint32_t url_len, points_error_t *error, struct points_renderer_t *renderer)
{
  if (!error)
    return nullptr;
  auto ret = std::make_unique<points_converter_data_source_t>(std::string(url, url_len), renderer->callbacks);
  if (ret->error.code != 0)
  {
    *error = ret->error;
    return nullptr;
  }
  return ret.release();
}

void points_converter_data_source_destroy(struct points_converter_data_source_t *converter_data_source)
{
  delete converter_data_source;
}

struct points_data_source_t points_converter_data_source_get(struct points_converter_data_source_t *converter_data_source)
{
  return converter_data_source->data_source;
}

void points_converter_data_source_request_aabb(struct points_converter_data_source_t *converter_data_source, points_converter_data_source_request_aabb_callback_t callback, void *user_ptr)
{
  auto callback_cpp = [callback, user_ptr](double aabb_min[3], double aabb_max[3]) { callback(aabb_min, aabb_max, user_ptr); };

  converter_data_source->processor.request_aabb(callback_cpp);
}

uint32_t points_converter_data_attribute_count(struct points_converter_data_source_t *converter_data_source)
{
  return converter_data_source->processor.attrib_name_registry_count();
}

uint32_t points_converter_data_get_attribute_name(struct points_converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_size)
{
  return converter_data_source->processor.attrib_name_registry_get(index, name, name_size);
}

void points_converter_data_set_rendered_attribute(struct points_converter_data_source_t *converter_data_source, const char *name, uint32_t name_len)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->next_attribute_name.assign(name, name_len);
}

void points_converter_data_source_set_viewport(struct points_converter_data_source_t *converter_data_source, int width, int height)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->viewport_width = width;
  converter_data_source->viewport_height = height;
}

void points_converter_data_source_set_pixel_error_threshold(struct points_converter_data_source_t *converter_data_source, double threshold)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->screen_fraction_threshold = threshold;
}

void points_converter_data_source_set_render_density_px(struct points_converter_data_source_t *converter_data_source, double density_px)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->render_density_px = density_px > 0.0 ? density_px : 0.01;
}

void points_converter_data_source_set_gpu_memory_budget(struct points_converter_data_source_t *converter_data_source, size_t budget_bytes)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->gpu_memory_budget = budget_bytes;
}

void points_converter_data_source_set_upload_budget_per_frame(struct points_converter_data_source_t *converter_data_source, size_t budget_bytes)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->upload_budget_per_frame = budget_bytes;
}

void points_converter_data_source_set_max_in_flight_io(struct points_converter_data_source_t *converter_data_source, int max_requests)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->max_in_flight_io = max_requests;
}

uint64_t points_converter_data_source_get_points_rendered(struct points_converter_data_source_t *converter_data_source)
{
  return converter_data_source->points_rendered_last_frame;
}

// True while the last frame left a node crossfade in progress. The dirty-driven host (renderer_wasm) polls
// this after a draw to re-arm the next frame so a fade-in/out keeps playing to completion even with no camera
// input or IO in flight. (The per-node LOD detail is distance-driven, not time-animated, so it is not here.)
uint8_t points_converter_data_source_is_animating(struct points_converter_data_source_t *cds)
{
  auto &t = cds->frame_timings;
  return (t.nodes_fading_in + t.nodes_fading_out) > 0 ? 1 : 0;
}

void points_converter_data_source_get_frame_timings(struct points_converter_data_source_t *cds, double *tree_walk_ms, double *buffer_reconciliation_ms, double *gpu_upload_ms, double *refine_strategy_ms, double *frontier_scheduling_ms,
                                             double *draw_emission_ms, double *eviction_ms, double *total_ms,
                                             int *registry_node_count, int *active_set_size, int *nodes_drawn,
                                             int *transitioning_count, int *nodes_evicted, int *nodes_reconcile_destroyed,
                                             int *walker_node_count, uint64_t *walker_total_points, int *walker_trees_to_load,
                                             int *io_in_flight)
{
  auto &t = cds->frame_timings;
  // Map new pipeline timings to old API parameters
  *tree_walk_ms = t.tree_walk_ms;
  *buffer_reconciliation_ms = t.build_render_list_ms;
  *gpu_upload_ms = t.gpu_upload_ms;
  *refine_strategy_ms = t.scan_classify_ms + t.schedule_io_ms;
  *frontier_scheduling_ms = t.normalize_ms;
  *draw_emission_ms = t.emit_ms;
  *eviction_ms = t.fade_ms;
  *total_ms = t.total_ms;
  if (registry_node_count) *registry_node_count = t.render_list_size;
  if (active_set_size) *active_set_size = t.nodes_uploaded;
  if (nodes_drawn) *nodes_drawn = t.nodes_drawn;
  if (transitioning_count) *transitioning_count = t.nodes_fading_in + t.nodes_fading_out;
  if (nodes_evicted) *nodes_evicted = 0;
  if (nodes_reconcile_destroyed) *nodes_reconcile_destroyed = 0;
  if (walker_node_count) *walker_node_count = t.walker_node_count;
  if (walker_total_points) *walker_total_points = t.walker_total_points;
  if (walker_trees_to_load) *walker_trees_to_load = t.walker_trees_to_load;
  if (io_in_flight) *io_in_flight = t.io_in_flight;
}

void points_converter_data_source_set_debug_transitions(struct points_converter_data_source_t *cds, uint8_t enabled)
{
  cds->debug_transitions = enabled;
}

void points_converter_data_source_set_show_bounding_boxes(struct points_converter_data_source_t *cds, uint8_t enabled)
{
  cds->show_bounding_boxes = enabled;
  cds->bbox_data_source->enabled = enabled;
}

struct points_data_source_t points_converter_data_source_get_bbox_data_source(struct points_converter_data_source_t *cds)
{
  return cds->bbox_data_source->data_source;
}

void points_converter_data_source_get_tight_aabb(struct points_converter_data_source_t *cds, double min[3], double max[3])
{
  auto &ta = cds->tight_aabb_accumulator;
  memcpy(min, &ta.min, sizeof(double) * 3);
  memcpy(max, &ta.max, sizeof(double) * 3);
}
