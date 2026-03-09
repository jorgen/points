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
#include "native_node_data_loader.hpp"
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fmt/printf.h>

#include "renderer.hpp"

using namespace points;
using namespace points::converter;

static bool less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs)
{
  if (lhs.lod == rhs.lod)
  {
    auto node_equals = lhs.node <=> rhs.node;
    if (node_equals == std::strong_ordering::equal)
    {
      return lhs.input_id < rhs.input_id;
    }
    return node_equals == std::strong_ordering::less;
  }
  return lhs.lod < rhs.lod;
}

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
}

void points_converter_data_source_t::add_to_frame(points_frame_camera_t *c_camera, points_to_render_t *to_render)
{
  using clock = std::chrono::high_resolution_clock;
  auto t_start = clock::now();

  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  bool new_attribute = false;
  double frac_threshold;
  size_t mem_budget;
  uint64_t pt_budget;
  {
    std::unique_lock<std::mutex> lock(mutex);
    new_attribute = current_attribute_name != next_attribute_name;
    current_attribute_name = next_attribute_name;
    frac_threshold = screen_fraction_threshold;
    mem_budget = gpu_memory_budget;
    pt_budget = point_budget;
  }

  // Handle attribute change
  if (new_attribute)
  {
    if (debug_transitions)
      fmt::print(stderr, "[transition-debug] === ATTRIBUTE CHANGE to '{}' ===\n", current_attribute_name);
    buffer_manager.handle_attribute_change(render_buffers, callbacks, node_loader, gpu_memory_used);

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
  walker.m_debug = debug_transitions;
  processor.walk_tree(walker);
  auto &walker_subsets = walker.m_new_nodes.point_subsets;
  std::sort(walker_subsets.begin(), walker_subsets.end(), less_than);
  frame_timings.walker_node_count = int(walker_subsets.size());
  frame_timings.walker_trees_to_load = int(walker.m_trees_to_load.size());
  {
    uint64_t total_pts = 0;
    for (auto &s : walker_subsets)
      total_pts += s.point_count.data;
    frame_timings.walker_total_points = total_pts;
  }
  auto t_after_tree_walk = clock::now();

  // Phase 1.5: Prepare fade-out retain set (before reconcile destroys buffers)
  auto &fade_out_retain = draw_emitter.prepare_fade_outs(walker_subsets);

  // Phase 2: Buffer reconciliation
  int reconcile_destroyed = 0;
  buffer_manager.reconcile(render_buffers, walker_subsets, callbacks, node_loader, gpu_memory_used,
                           debug_transitions, &reconcile_destroyed, fade_out_retain);
  frame_timings.nodes_reconcile_destroyed = reconcile_destroyed;
  auto t_after_reconciliation = clock::now();

  // Phase 3: GPU upload
  auto tree_config = processor.tree_config();
  size_t upload_limit = mem_budget + mem_budget / 5;
  buffer_manager.upload_ready(render_buffers, callbacks, node_loader, gpu_memory_used,
                              upload_limit, 4, camera, current_attribute_name,
                              current_attr_min, current_attr_max);
  auto t_after_upload = clock::now();

  // Phase 4: Update registry from walker
  node_registry.update_from_walker(walker_subsets, walker.m_new_nodes.parent_child_edges, render_buffers, fade_out_retain);
  frame_timings.registry_node_count = int(node_registry.nodes().size());
  if (debug_transitions)
  {
    fmt::print(stderr, "[transition-debug] registry: {} nodes, {} roots\n",
               node_registry.nodes().size(), node_registry.roots().size());
  }

  // Update tight AABB from current registry nodes
  for (auto &[node_id, node] : node_registry.nodes())
  {
    tight_aabb_accumulator.min = glm::min(tight_aabb_accumulator.min, node.tight_aabb.min);
    tight_aabb_accumulator.max = glm::max(tight_aabb_accumulator.max, node.tight_aabb.max);
  }

  // Phase 5: Node selection (refine strategy + point budget)
  // Check if any node is transitioning to protect all from budget-based collapse
  bool any_transitioning = false;
  for (auto &[node_id, node] : node_registry.nodes())
  {
    for (int idx : node.buffer_indices)
    {
      auto &rb = *render_buffers[idx];
      if (rb.awaiting_new_color || rb.old_color_valid)
      {
        any_transitioning = true;
        break;
      }
    }
    if (any_transitioning)
      break;
  }

  selection_params_t sel_params;
  sel_params.camera_position = camera_position;
  sel_params.memory_budget = mem_budget;
  sel_params.point_budget = pt_budget;
  auto selection = node_selector.select(node_registry, sel_params, debug_transitions, any_transitioning);
  auto t_after_refine = clock::now();
  frame_timings.active_set_size = int(selection.active_set.size());

  if (debug_transitions)
  {
    int transitioning = 0;
    for (auto &node_id : selection.active_set)
    {
      auto *node = node_registry.get_node(node_id);
      if (!node)
        continue;
      for (int idx : node->buffer_indices)
      {
        auto &rb = *render_buffers[idx];
        if (rb.awaiting_new_color || rb.old_color_valid)
        {
          transitioning++;
          break;
        }
      }
    }
    fmt::print(stderr, "[transition-debug] selection: active={} memory={}/{} points={}/{} transitioning={} reconcile_destroyed={}\n",
               selection.active_set.size(), selection.total_memory, mem_budget,
               selection.total_points, pt_budget, transitioning, reconcile_destroyed);
    frame_timings.transitioning_count = transitioning;

    if (!previous_active_set.empty())
    {
      for (auto &prev_id : previous_active_set)
      {
        if (!selection.active_set.count(prev_id))
        {
          auto *node = node_registry.get_node(prev_id);
          if (node)
            fmt::print(stderr, "[transition-debug] DISAPPEARED: node lod={} rendered={} mem={}\n",
                       node->lod, node->all_rendered, node->gpu_memory_size);
          else
            fmt::print(stderr, "[transition-debug] DISAPPEARED: node (not in registry)\n");
        }
      }
    }
    previous_active_set = selection.active_set;
  }

  // Collect bounding boxes for active nodes
  if (show_bounding_boxes)
  {
    std::vector<node_bbox_t> loose_boxes;
    std::vector<node_bbox_t> tight_boxes;
    for (auto &node_id : selection.active_set)
    {
      auto *node = node_registry.get_node(node_id);
      if (!node)
        continue;
      loose_boxes.push_back({node->aabb.min, node->aabb.max});
      tight_boxes.push_back({node->tight_aabb.min, node->tight_aabb.max});
    }
    bbox_data_source->update_boxes(loose_boxes, tight_boxes);
  }

  // Phase 6: Frontier I/O scheduling
  buffer_manager.schedule_io(render_buffers, node_registry, selection, tree_config, node_loader, 20);
  auto t_after_frontier = clock::now();

  // Phase 7: Draw emission
  auto draw_result = draw_emitter.emit(render_buffers, node_registry, selection, callbacks, camera, tree_config, to_render, debug_transitions);
  points_rendered_last_frame = draw_result.points_rendered;
  frame_timings.nodes_drawn = draw_result.nodes_drawn;
  gpu_memory_used -= draw_result.freed_gpu_memory;
  auto t_after_draw = clock::now();

  // Phase 8: Eviction
  int evicted_count = 0;
  buffer_manager.evict(render_buffers, node_registry, selection, camera_position,
                       gpu_memory_used, upload_limit, callbacks, node_loader,
                       debug_transitions, &evicted_count, fade_out_retain);
  frame_timings.nodes_evicted = evicted_count;

  auto t_end = clock::now();

  auto to_ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
  frame_timings.tree_walk_ms = to_ms(t_after_tree_walk - t_start);
  frame_timings.buffer_reconciliation_ms = to_ms(t_after_reconciliation - t_after_tree_walk);
  frame_timings.gpu_upload_ms = to_ms(t_after_upload - t_after_reconciliation);
  frame_timings.refine_strategy_ms = to_ms(t_after_refine - t_after_upload);
  frame_timings.frontier_scheduling_ms = to_ms(t_after_frontier - t_after_refine);
  frame_timings.draw_emission_ms = to_ms(t_after_draw - t_after_frontier);
  frame_timings.eviction_ms = to_ms(t_end - t_after_draw);
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

void points_converter_data_source_set_gpu_memory_budget(struct points_converter_data_source_t *converter_data_source, size_t budget_bytes)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->gpu_memory_budget = budget_bytes;
}

uint64_t points_converter_data_source_get_points_rendered(struct points_converter_data_source_t *converter_data_source)
{
  return converter_data_source->points_rendered_last_frame;
}

void points_converter_data_source_get_frame_timings(struct points_converter_data_source_t *cds, double *tree_walk_ms, double *buffer_reconciliation_ms, double *gpu_upload_ms, double *refine_strategy_ms, double *frontier_scheduling_ms,
                                             double *draw_emission_ms, double *eviction_ms, double *total_ms,
                                             int *registry_node_count, int *active_set_size, int *nodes_drawn,
                                             int *transitioning_count, int *nodes_evicted, int *nodes_reconcile_destroyed,
                                             int *walker_node_count, uint64_t *walker_total_points, int *walker_trees_to_load)
{
  auto &t = cds->frame_timings;
  *tree_walk_ms = t.tree_walk_ms;
  *buffer_reconciliation_ms = t.buffer_reconciliation_ms;
  *gpu_upload_ms = t.gpu_upload_ms;
  *refine_strategy_ms = t.refine_strategy_ms;
  *frontier_scheduling_ms = t.frontier_scheduling_ms;
  *draw_emission_ms = t.draw_emission_ms;
  *eviction_ms = t.eviction_ms;
  *total_ms = t.total_ms;
  if (registry_node_count) *registry_node_count = t.registry_node_count;
  if (active_set_size) *active_set_size = t.active_set_size;
  if (nodes_drawn) *nodes_drawn = t.nodes_drawn;
  if (transitioning_count) *transitioning_count = t.transitioning_count;
  if (nodes_evicted) *nodes_evicted = t.nodes_evicted;
  if (nodes_reconcile_destroyed) *nodes_reconcile_destroyed = t.nodes_reconcile_destroyed;
  if (walker_node_count) *walker_node_count = t.walker_node_count;
  if (walker_total_points) *walker_total_points = t.walker_total_points;
  if (walker_trees_to_load) *walker_trees_to_load = t.walker_trees_to_load;
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
