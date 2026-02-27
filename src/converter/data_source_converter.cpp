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

namespace points::converter
{

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

converter_data_source_t::converter_data_source_t(const std::string &url, render::callback_manager_t &callbacks)
  : url(url)
  , processor(url, file_existence_requirement_t::exist, error)
  , callbacks(callbacks)
{
  if (error.code != 0)
  {
    return;
  }
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr)
  {
    auto *thiz = static_cast<converter_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };

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

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
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

  frustum_tree_walker_t walker(camera.view_projection, lod_params, std::vector<std::string>({std::string("xyz"), current_attribute_name}));
  processor.walk_tree(walker);
  auto &walker_subsets = walker.m_new_nodes.point_subsets;
  std::sort(walker_subsets.begin(), walker_subsets.end(), less_than);
  auto t_after_tree_walk = clock::now();

  // Phase 2: Buffer reconciliation
  buffer_manager.reconcile(render_buffers, walker_subsets, callbacks, node_loader, gpu_memory_used);
  auto t_after_reconciliation = clock::now();

  // Phase 3: GPU upload
  auto tree_config = processor.tree_config();
  size_t upload_limit = mem_budget + mem_budget / 5;
  buffer_manager.upload_ready(render_buffers, callbacks, node_loader, gpu_memory_used,
                              upload_limit, 4, camera, current_attribute_name,
                              current_attr_min, current_attr_max);
  auto t_after_upload = clock::now();

  // Phase 4: Update registry from walker
  node_registry.update_from_walker(walker_subsets, walker.m_new_nodes.parent_child_edges, render_buffers);

  // Phase 5: Node selection (refine strategy + point budget)
  selection_params_t sel_params;
  sel_params.camera_position = camera_position;
  sel_params.memory_budget = mem_budget;
  sel_params.point_budget = pt_budget;
  auto selection = node_selector.select(node_registry, sel_params);
  auto t_after_refine = clock::now();

  // Phase 6: Frontier I/O scheduling
  buffer_manager.schedule_io(render_buffers, node_registry, selection, tree_config, node_loader, 20);
  auto t_after_frontier = clock::now();

  // Phase 7: Draw emission
  auto draw_result = draw_emitter.emit(render_buffers, node_registry, selection, callbacks, camera, tree_config, to_render);
  points_rendered_last_frame = draw_result.points_rendered;
  auto t_after_draw = clock::now();

  // Phase 8: Eviction
  buffer_manager.evict(render_buffers, node_registry, selection, camera_position,
                       gpu_memory_used, upload_limit, callbacks, node_loader);

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

struct converter_data_source_t *converter_data_source_create(const char *url, uint32_t url_len, error_t *error, struct render::renderer_t *renderer)
{
  if (!error)
    return nullptr;
  auto ret = std::make_unique<converter_data_source_t>(std::string(url, url_len), renderer->callbacks);
  if (ret->error.code != 0)
  {
    *error = ret->error;
    return nullptr;
  }
  return ret.release();
}

void converter_data_source_destroy(struct converter_data_source_t *converter_data_source)
{
  delete converter_data_source;
}

struct render::data_source_t converter_data_source_get(struct converter_data_source_t *converter_data_source)
{
  return converter_data_source->data_source;
}

void converter_data_source_request_aabb(struct converter_data_source_t *converter_data_source, converter_data_source_request_aabb_callback_t callback, void *user_ptr)
{
  auto callback_cpp = [callback, user_ptr](double aabb_min[3], double aabb_max[3]) { callback(aabb_min, aabb_max, user_ptr); };

  converter_data_source->processor.request_aabb(callback_cpp);
}

uint32_t converter_data_attribute_count(struct converter_data_source_t *converter_data_source)
{
  return converter_data_source->processor.attrib_name_registry_count();
}

uint32_t converter_data_get_attribute_name(struct converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_size)
{
  return converter_data_source->processor.attrib_name_registry_get(index, name, name_size);
}

void converter_data_set_rendered_attribute(struct converter_data_source_t *converter_data_source, const char *name, uint32_t name_len)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->next_attribute_name.assign(name, name_len);
}

void converter_data_source_set_viewport(struct converter_data_source_t *converter_data_source, int width, int height)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->viewport_width = width;
  converter_data_source->viewport_height = height;
}

void converter_data_source_set_pixel_error_threshold(struct converter_data_source_t *converter_data_source, double threshold)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->screen_fraction_threshold = threshold;
}

void converter_data_source_set_gpu_memory_budget(struct converter_data_source_t *converter_data_source, size_t budget_bytes)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->gpu_memory_budget = budget_bytes;
}

uint64_t converter_data_source_get_points_rendered(struct converter_data_source_t *converter_data_source)
{
  return converter_data_source->points_rendered_last_frame;
}

void converter_data_source_get_frame_timings(struct converter_data_source_t *cds, double *tree_walk_ms, double *buffer_reconciliation_ms, double *gpu_upload_ms, double *refine_strategy_ms, double *frontier_scheduling_ms,
                                             double *draw_emission_ms, double *eviction_ms, double *total_ms)
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
}

} // namespace points::converter
