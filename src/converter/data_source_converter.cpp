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
#include "native_node_data_loader.hpp"
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>

#include <algorithm>
#include <chrono>
#include <fmt/printf.h>
#include <unordered_map>
#include <unordered_set>

#include "renderer.hpp"

namespace points::converter
{
bool has_rendered = false;

template <typename buffer_data_t>
void initialize_buffer(render::callback_manager_t &callbacks, std::vector<buffer_data_t> &data_vector, render::buffer_type_t buffer_type, type_t type, components_t components, render::buffer_t &buffer)
{
  (void)callbacks;
  (void)buffer_type;
  (void)type;
  (void)components;
  (void)buffer;
  assert(data_vector.size());
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

  if (processor.attrib_name_registry_count() > 2)
  {
    char buffer[256];
    auto str_size = processor.attrib_name_registry_get(1, buffer, sizeof(buffer));
    next_attribute_name.assign(buffer, str_size);
  }
}

bool less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs)
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

void converter_data_source_t::destroy_gpu_buffer(gpu_node_buffer_t &buf)
{
  if (buf.load_handle != render::invalid_load_handle)
  {
    node_loader->cancel(buf.load_handle);
    buf.load_handle = render::invalid_load_handle;
  }
  if (buf.rendered)
  {
    for (auto &rb : buf.render_buffers)
      callbacks.do_destroy_buffer(rb);
    if (buf.gpu_memory_size > 0)
      gpu_memory_used -= buf.gpu_memory_size;
    buf.rendered = false;
    buf.gpu_memory_size = 0;
  }
}

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  using clock = std::chrono::high_resolution_clock;
  auto t_start = clock::now();

  (void)to_render;
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  bool new_attribute = false;
  int vp_height;
  double pix_threshold;
  double eff_threshold;
  size_t mem_budget;
  {
    std::unique_lock<std::mutex> lock(mutex);
    new_attribute = current_attribute_name != next_attribute_name;
    current_attribute_name = next_attribute_name;
    vp_height = viewport_height;
    pix_threshold = pixel_error_threshold;
    eff_threshold = effective_pixel_error_threshold;
    mem_budget = gpu_memory_budget;
  }
  if (new_attribute)
  {
    for (auto &rb : render_buffers)
      destroy_gpu_buffer(*rb);
    render_buffers.clear();
    gpu_memory_used = 0;
  }

  glm::dvec3 camera_position = glm::dvec3(camera.inverse_view[3]);
  lod_params_t lod_params;
  lod_params.camera_position = camera_position;
  lod_params.projection = camera.projection;
  lod_params.screen_height = double(vp_height);
  lod_params.pixel_error_threshold = eff_threshold;

  back_buffer = std::make_shared<frustum_tree_walker_t>(camera.view_projection, lod_params, std::vector<std::string>({std::string("xyz"), current_attribute_name}));
  auto copy_back_buffer_ptr = back_buffer;
  processor.walk_tree(std::move(copy_back_buffer_ptr));
  back_buffer->wait_done();
  auto &buffer = back_buffer->m_new_nodes.point_subsets;
  std::sort(buffer.begin(), buffer.end(), less_than);
  auto t_after_tree_walk = clock::now();

  std::vector<std::unique_ptr<gpu_node_buffer_t>> new_render_buffers;
  new_render_buffers.reserve(buffer.size());

  auto render_buffers_it = render_buffers.begin();
  int i;
  for (i = 0; i < int(buffer.size()) && render_buffers_it != render_buffers.end(); i++)
  {
    auto &node = buffer[i];
    while (render_buffers_it != render_buffers.end() && less_than(render_buffers_it->get()->node_info, node))
    {
      destroy_gpu_buffer(*render_buffers_it->get());
      render_buffers_it++;
    }
    if (render_buffers_it == render_buffers.end())
    {
      break;
    }
    if (less_than(node, render_buffers_it->get()->node_info))
    {
      auto &new_buffer = new_render_buffers.emplace_back(new gpu_node_buffer_t());
      new_buffer->node_info = node;
    }
    else
    {
      (*render_buffers_it)->node_info.frustum_visible = node.frustum_visible;
      new_render_buffers.emplace_back(std::move(*render_buffers_it));
      render_buffers_it++;
    }
  }
  while (render_buffers_it != render_buffers.end())
  {
    destroy_gpu_buffer(*render_buffers_it->get());
    render_buffers_it++;
  }
  for (; i < int(buffer.size()); i++)
  {
    auto &node = buffer[i];
    auto &new_buffer = new_render_buffers.emplace_back(new gpu_node_buffer_t());
    new_buffer->node_info = node;
  }

  render_buffers = std::move(new_render_buffers);
  auto t_after_reconciliation = clock::now();

  // Initialize GPU data for buffers that have finished loading (LOD-prioritized, limited per frame)
  auto tree_config = processor.tree_config();

  struct upload_candidate_t
  {
    int index;
    int lod;
  };
  std::vector<upload_candidate_t> upload_candidates;
  for (int idx = 0; idx < int(render_buffers.size()); idx++)
  {
    auto &rb = *render_buffers[idx];
    if (rb.load_handle != render::invalid_load_handle && node_loader->is_ready(rb.load_handle))
      upload_candidates.push_back({idx, rb.node_info.lod});
  }

  std::sort(upload_candidates.begin(), upload_candidates.end(), [](const auto &a, const auto &b) { return a.lod > b.lod; });

  size_t upload_limit = mem_budget + mem_budget / 5; // 120% of budget: headroom for prefetch
  constexpr int max_uploads_per_frame = 4;
  int uploads_done = 0;
  for (auto &candidate : upload_candidates)
  {
    if (uploads_done >= max_uploads_per_frame)
      break;
    if (gpu_memory_used >= upload_limit)
      break;
    auto &render_buffer = *render_buffers[candidate.index];

    auto loaded = node_loader->get_data(render_buffer.load_handle);
    render_buffer.load_handle = render::invalid_load_handle;
    render_buffer.point_count = loaded.point_count;
    render_buffer.offset = loaded.offset;
    render_buffer.draw_type = loaded.draw_type;

    callbacks.do_create_buffer(render_buffer.render_buffers[0], points::render::buffer_type_vertex);
    callbacks.do_initialize_buffer(render_buffer.render_buffers[0], loaded.vertex_type, loaded.vertex_components, int(loaded.vertex_data_size), loaded.vertex_data);

    callbacks.do_create_buffer(render_buffer.render_buffers[1], points::render::buffer_type_vertex);
    callbacks.do_initialize_buffer(render_buffer.render_buffers[1], loaded.attribute_type, loaded.attribute_components, int(loaded.attribute_data_size), loaded.attribute_data);

    render_buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(render_buffer.offset));

    callbacks.do_create_buffer(render_buffer.render_buffers[2], points::render::buffer_type_uniform);
    callbacks.do_initialize_buffer(render_buffer.render_buffers[2], type_r32, points::components_4x4, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

    render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
    render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
    render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
    render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
    render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
    render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;

    size_t buf_mem = loaded.vertex_data_size + loaded.attribute_data_size + sizeof(render_buffer.camera_view);
    render_buffer.gpu_memory_size = buf_mem;
    gpu_memory_used += buf_mem;

    render_buffer.rendered = true;
    loaded.release();
    uploads_done++;
  }
  auto t_after_upload = clock::now();

  // ===== Refine Strategy: Coarsest-to-finest with distance-based budget =====

  struct node_id_hash
  {
    size_t operator()(const node_id_t &id) const
    {
      uint64_t v;
      memcpy(&v, &id, sizeof(v));
      return std::hash<uint64_t>()(v);
    }
  };
  struct node_id_equal
  {
    bool operator()(const node_id_t &a, const node_id_t &b) const
    {
      return (a <=> b) == std::strong_ordering::equal;
    }
  };

  // Phase 1: Group render buffers by node_id
  struct node_group_t
  {
    node_id_t parent_id;
    node_aabb_t aabb;
    node_aabb_t tight_aabb;
    bool frustum_visible;
    std::vector<int> buffer_indices;
    bool all_rendered;
    size_t total_gpu_memory;
  };

  std::unordered_map<node_id_t, node_group_t, node_id_hash, node_id_equal> node_map;
  for (int idx = 0; idx < int(render_buffers.size()); idx++)
  {
    auto &rb = *render_buffers[idx];
    auto &group = node_map[rb.node_info.node];
    if (group.buffer_indices.empty())
    {
      group.parent_id = rb.node_info.parent;
      group.aabb = rb.node_info.aabb;
      group.tight_aabb = rb.node_info.tight_aabb;
      group.frustum_visible = rb.node_info.frustum_visible;
      group.all_rendered = true;
      group.total_gpu_memory = 0;
    }
    group.buffer_indices.push_back(idx);
    if (!rb.rendered)
      group.all_rendered = false;
    group.total_gpu_memory += rb.gpu_memory_size;
  }

  // Build children map: parent_id -> child_node_ids (only where parent exists in node_map)
  std::unordered_map<node_id_t, std::vector<node_id_t>, node_id_hash, node_id_equal> children_map;
  for (auto &[node_id, group] : node_map)
  {
    if (node_map.count(group.parent_id))
      children_map[group.parent_id].push_back(node_id);
  }

  // Phase 2: Iterative coarsest-to-finest refine
  // Start with root-level nodes: those whose parent is not in node_map
  std::unordered_set<node_id_t, node_id_hash, node_id_equal> active_set;
  size_t active_memory = 0;
  for (auto &[node_id, group] : node_map)
  {
    if (!node_map.count(group.parent_id))
    {
      active_set.insert(node_id);
      if (group.all_rendered)
        active_memory += group.total_gpu_memory;
    }
  }

  // Iteratively expand active nodes into children, prioritized by distance to camera.
  // Children don't need all siblings loaded — rendered children are added alongside the
  // parent (parent covers gaps). When all children are ready, the parent is removed.
  bool changed = true;
  while (changed)
  {
    changed = false;

    // Sort active nodes by distance to camera — closest expand first within budget
    struct expansion_t
    {
      node_id_t node_id;
      double distance;
    };
    std::vector<expansion_t> candidates;
    for (auto &node_id : active_set)
    {
      auto children_it = children_map.find(node_id);
      if (children_it == children_map.end())
        continue;
      auto &group = node_map[node_id];
      glm::dvec3 center = (group.tight_aabb.min + group.tight_aabb.max) * 0.5;
      double dist = glm::length(center - camera_position);
      candidates.push_back({node_id, dist});
    }
    std::sort(candidates.begin(), candidates.end(), [](const expansion_t &a, const expansion_t &b) { return a.distance < b.distance; });

    std::vector<node_id_t> to_remove;
    std::vector<node_id_t> to_add;
    for (auto &candidate : candidates)
    {
      auto &children = children_map[candidate.node_id];
      bool all_children_ready = true;
      size_t all_children_memory = 0;
      std::vector<node_id_t> rendered_children;
      for (auto &child_id : children)
      {
        auto &child_group = node_map[child_id];
        if (child_group.all_rendered)
        {
          rendered_children.push_back(child_id);
          all_children_memory += child_group.total_gpu_memory;
        }
        else
        {
          all_children_ready = false;
        }
      }
      if (rendered_children.empty())
        continue;

      if (all_children_ready)
      {
        // Clean swap: remove parent, add all children
        size_t parent_memory = node_map[candidate.node_id].total_gpu_memory;
        size_t new_active_memory = active_memory - parent_memory + all_children_memory;
        if (new_active_memory > mem_budget)
          continue;
        to_remove.push_back(candidate.node_id);
        for (auto &child_id : rendered_children)
          to_add.push_back(child_id);
        active_memory = new_active_memory;
        changed = true;
      }
      else
      {
        // Partial: keep parent as fallback, add rendered children alongside it
        size_t new_children_memory = 0;
        std::vector<node_id_t> new_children;
        for (auto &child_id : rendered_children)
        {
          if (!active_set.count(child_id))
          {
            new_children.push_back(child_id);
            new_children_memory += node_map[child_id].total_gpu_memory;
          }
        }
        if (new_children.empty())
          continue;
        if (active_memory + new_children_memory > mem_budget)
          continue;
        for (auto &child_id : new_children)
          to_add.push_back(child_id);
        active_memory += new_children_memory;
        changed = true;
      }
    }
    for (auto &id : to_remove)
      active_set.erase(id);
    for (auto &id : to_add)
      active_set.insert(id);
  }
  auto t_after_refine = clock::now();

  // Start I/O for frontier nodes only (active nodes + one-level lookahead)
  {
    struct frontier_candidate_t
    {
      int index;
      int lod;
    };
    std::vector<frontier_candidate_t> frontier;

    for (auto &node_id : active_set)
    {
      auto &group = node_map[node_id];
      for (int idx : group.buffer_indices)
      {
        auto &rb = *render_buffers[idx];
        if (rb.load_handle == render::invalid_load_handle && !rb.rendered)
          frontier.push_back({idx, rb.node_info.lod});
      }
      // One-level lookahead: children of active nodes
      auto children_it = children_map.find(node_id);
      if (children_it != children_map.end())
      {
        for (auto &child_id : children_it->second)
        {
          auto child_group_it = node_map.find(child_id);
          if (child_group_it == node_map.end())
            continue;
          for (int idx : child_group_it->second.buffer_indices)
          {
            auto &rb = *render_buffers[idx];
            if (rb.load_handle == render::invalid_load_handle && !rb.rendered)
              frontier.push_back({idx, rb.node_info.lod});
          }
        }
      }
    }

    std::sort(frontier.begin(), frontier.end(), [](const auto &a, const auto &b) { return a.lod > b.lod; });

    std::unordered_set<int> started;
    constexpr int max_requests_per_frame = 20;
    int requests_started = 0;
    for (auto &fc : frontier)
    {
      if (requests_started >= max_requests_per_frame)
        break;
      if (!started.insert(fc.index).second)
        continue;
      auto &rb = *render_buffers[fc.index];
      native_load_request_t req;
      memcpy(req.format, rb.node_info.format, sizeof(req.format));
      memcpy(req.locations, rb.node_info.locations, sizeof(req.locations));
      req.tree_config = tree_config;
      rb.load_handle = node_loader->request_load(&req, sizeof(req));
      requests_started++;
    }
  }
  auto t_after_frontier = clock::now();

  // Phase 3: Distance sort
  struct active_node_info_t
  {
    node_id_t node_id;
    double distance;
    size_t gpu_memory;
    bool frustum_visible;
  };
  std::vector<active_node_info_t> sorted_active;
  sorted_active.reserve(active_set.size());
  for (auto &node_id : active_set)
  {
    auto &group = node_map[node_id];
    glm::dvec3 center = (group.tight_aabb.min + group.tight_aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    sorted_active.push_back({node_id, dist, group.total_gpu_memory, group.frustum_visible});
  }
  std::sort(sorted_active.begin(), sorted_active.end(), [](const active_node_info_t &a, const active_node_info_t &b) { return a.distance < b.distance; });

  // Phase 4: Emit draw commands for ALL active nodes that are rendered and frustum-visible
  uint64_t frame_point_count = 0;
  for (auto &info : sorted_active)
  {
    if (!info.frustum_visible)
      continue;
    auto &group = node_map[info.node_id];
    for (int idx : group.buffer_indices)
    {
      auto &render_buffer = *render_buffers[idx];
      if (!render_buffer.rendered)
        continue;
      auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
      render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
      callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);
      render::draw_group_t draw_group = {render_buffer.draw_type, render_buffer.render_list, 3, int(render_buffer.point_count), render_buffer.node_info.lod};
      to_render_add_render_group(to_render, draw_group);
      frame_point_count += render_buffer.point_count;
    }
  }
  points_rendered_last_frame = frame_point_count;
  auto t_after_draw = clock::now();

  // Phase 5: Distance-based eviction with parent preservation
  // Build non-evictable set: active nodes + ancestors (children are prefetched and evictable)
  std::unordered_set<node_id_t, node_id_hash, node_id_equal> non_evictable;
  for (auto &node_id : active_set)
  {
    non_evictable.insert(node_id);
    // Mark ancestors as non-evictable (needed as fallback)
    node_id_t current = node_id;
    while (true)
    {
      auto it = node_map.find(current);
      if (it == node_map.end())
        break;
      auto parent = it->second.parent_id;
      if (!node_map.count(parent))
        break;
      if (!non_evictable.insert(parent).second)
        break;
      current = parent;
    }
  }

  struct evictable_node_t
  {
    node_id_t node_id;
    double distance;
  };
  std::vector<evictable_node_t> evictable;
  for (auto &[node_id, group] : node_map)
  {
    if (non_evictable.count(node_id))
      continue;
    if (!group.all_rendered)
      continue;
    glm::dvec3 center = (group.tight_aabb.min + group.tight_aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    evictable.push_back({node_id, dist});
  }
  std::sort(evictable.begin(), evictable.end(), [](const evictable_node_t &a, const evictable_node_t &b) { return a.distance > b.distance; });

  for (auto &ev : evictable)
  {
    if (gpu_memory_used <= upload_limit)
      break;
    auto &group = node_map[ev.node_id];
    for (int idx : group.buffer_indices)
      destroy_gpu_buffer(*render_buffers[idx]);
  }

  // Phase 6: Dynamic pixel error threshold adjustment
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (auto_adjust_threshold)
    {
      if (gpu_memory_used > mem_budget * 9 / 10)
        effective_pixel_error_threshold *= 1.1;
      else if (gpu_memory_used < mem_budget / 2 && effective_pixel_error_threshold > pix_threshold)
        effective_pixel_error_threshold = std::max(pix_threshold, effective_pixel_error_threshold * 0.9);
      effective_pixel_error_threshold = std::clamp(effective_pixel_error_threshold, 1.0, 1000.0);
    }
    else
    {
      effective_pixel_error_threshold = pix_threshold;
    }
  }
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
  converter_data_source->pixel_error_threshold = threshold;
  converter_data_source->effective_pixel_error_threshold = std::max(converter_data_source->effective_pixel_error_threshold, threshold);
}

void converter_data_source_set_auto_adjust_threshold(struct converter_data_source_t *converter_data_source, bool enabled)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->auto_adjust_threshold = enabled;
}

void converter_data_source_set_gpu_memory_budget(struct converter_data_source_t *converter_data_source, size_t budget_bytes)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->gpu_memory_budget = budget_bytes;
}

double converter_data_source_get_effective_pixel_error_threshold(struct converter_data_source_t *converter_data_source)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  return converter_data_source->effective_pixel_error_threshold;
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
