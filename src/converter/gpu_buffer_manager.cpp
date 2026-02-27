/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#include "gpu_buffer_manager.hpp"
#include "native_node_data_loader.hpp"
#include <points/common/format.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace points::converter
{

static bool less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs)
{
  if (lhs.lod == rhs.lod)
  {
    auto node_equals = lhs.node <=> rhs.node;
    if (node_equals == std::strong_ordering::equal)
      return lhs.input_id < rhs.input_id;
    return node_equals == std::strong_ordering::less;
  }
  return lhs.lod < rhs.lod;
}

static void destroy_gpu_buffer(gpu_node_buffer_t &buf, render::callback_manager_t &callbacks,
                                std::unique_ptr<render::node_data_loader_t> &node_loader, size_t &gpu_memory_used)
{
  if (buf.load_handle != render::invalid_load_handle)
  {
    node_loader->cancel(buf.load_handle);
    buf.load_handle = render::invalid_load_handle;
  }
  if (buf.rendered)
  {
    for (auto &rb : buf.render_buffers)
    {
      if (rb.user_ptr)
        callbacks.do_destroy_buffer(rb);
    }
    if (buf.gpu_memory_size > 0)
      gpu_memory_used -= buf.gpu_memory_size;
    buf.rendered = false;
    buf.gpu_memory_size = 0;
  }
  if (buf.old_color_valid)
  {
    callbacks.do_destroy_buffer(buf.old_color_buffer);
    buf.old_color_valid = false;
    buf.old_color_memory = 0;
  }
  if (buf.params_buffer.user_ptr)
  {
    callbacks.do_destroy_buffer(buf.params_buffer);
  }
  buf.awaiting_new_color = false;
  buf.fade_frame = 0;
  buf.crossfade_frame = 0;
}

static std::shared_ptr<uint8_t[]> normalize_attribute_to_float(const void *data, uint32_t data_size, type_t type, components_t components,
                                                                uint32_t point_count, double global_min, double global_max,
                                                                uint32_t &out_size)
{
  double range = global_max - global_min;
  if (range <= 0.0)
    range = 1.0;
  double inv_range = 1.0 / range;

  uint32_t comp_count = static_cast<uint32_t>(components);
  out_size = point_count * comp_count * sizeof(float);
  auto result = std::make_shared<uint8_t[]>(out_size);
  auto *dst = reinterpret_cast<float *>(result.get());
  auto *src = static_cast<const uint8_t *>(data);

  int type_size = 0;
  switch (type)
  {
  case type_u8: case type_i8: type_size = 1; break;
  case type_u16: case type_i16: type_size = 2; break;
  case type_u32: case type_i32: case type_r32: type_size = 4; break;
  case type_u64: case type_i64: case type_r64: type_size = 8; break;
  default: type_size = 1; break;
  }

  uint32_t elem_size = static_cast<uint32_t>(type_size) * comp_count;
  uint32_t actual_count = std::min(point_count, data_size / elem_size);

  for (uint32_t i = 0; i < actual_count; i++)
  {
    for (uint32_t c = 0; c < comp_count; c++)
    {
      const uint8_t *elem = src + i * elem_size + c * type_size;
      double val = 0.0;
      switch (type)
      {
      case type_u8:  { uint8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case type_i8:  { int8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case type_u16: { uint16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case type_i16: { int16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case type_u32: { uint32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case type_i32: { int32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case type_r32: { float v; memcpy(&v, elem, 4); val = double(v); break; }
      case type_u64: { uint64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case type_i64: { int64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case type_r64: { double v; memcpy(&v, elem, 8); val = v; break; }
      default: break;
      }
      float normalized = static_cast<float>((val - global_min) * inv_range);
      if (normalized < 0.0f) normalized = 0.0f;
      if (normalized > 1.0f) normalized = 1.0f;
      dst[i * comp_count + c] = normalized;
    }
  }

  return result;
}

void gpu_buffer_manager_t::reconcile(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                     const std::vector<tree_walker_data_t> &walker_subsets,
                                     render::callback_manager_t &callbacks,
                                     std::unique_ptr<render::node_data_loader_t> &node_loader,
                                     size_t &gpu_memory_used)
{
  std::vector<std::unique_ptr<gpu_node_buffer_t>> new_render_buffers;
  new_render_buffers.reserve(walker_subsets.size());

  auto render_buffers_it = render_buffers.begin();
  int i;
  for (i = 0; i < int(walker_subsets.size()) && render_buffers_it != render_buffers.end(); i++)
  {
    auto &node = walker_subsets[i];
    while (render_buffers_it != render_buffers.end() && less_than(render_buffers_it->get()->node_info, node))
    {
      destroy_gpu_buffer(*render_buffers_it->get(), callbacks, node_loader, gpu_memory_used);
      render_buffers_it++;
    }
    if (render_buffers_it == render_buffers.end())
      break;
    if (less_than(node, render_buffers_it->get()->node_info))
    {
      auto &new_buffer = new_render_buffers.emplace_back(new gpu_node_buffer_t());
      new_buffer->node_info = node;
    }
    else
    {
      auto &existing = *render_buffers_it;
      existing->node_info.frustum_visible = node.frustum_visible;
      memcpy(existing->node_info.format, node.format, sizeof(node.format));
      memcpy(existing->node_info.locations, node.locations, sizeof(node.locations));
      new_render_buffers.emplace_back(std::move(existing));
      render_buffers_it++;
    }
  }
  while (render_buffers_it != render_buffers.end())
  {
    destroy_gpu_buffer(*render_buffers_it->get(), callbacks, node_loader, gpu_memory_used);
    render_buffers_it++;
  }
  for (; i < int(walker_subsets.size()); i++)
  {
    auto &node = walker_subsets[i];
    auto &new_buffer = new_render_buffers.emplace_back(new gpu_node_buffer_t());
    new_buffer->node_info = node;
  }

  render_buffers = std::move(new_render_buffers);
}

int gpu_buffer_manager_t::upload_ready(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                       render::callback_manager_t &callbacks,
                                       std::unique_ptr<render::node_data_loader_t> &node_loader,
                                       size_t &gpu_memory_used,
                                       size_t upload_limit,
                                       int max_uploads,
                                       const render::frame_camera_cpp_t &camera,
                                       const std::string &current_attribute_name,
                                       double attr_min, double attr_max)
{
  (void)current_attribute_name;

  struct upload_candidate_t
  {
    int index;
    int lod;
    bool awaiting;
  };
  std::vector<upload_candidate_t> upload_candidates;
  for (int idx = 0; idx < int(render_buffers.size()); idx++)
  {
    auto &rb = *render_buffers[idx];
    if (rb.load_handle != render::invalid_load_handle && node_loader->is_ready(rb.load_handle))
    {
      upload_candidates.push_back({idx, rb.node_info.lod, rb.awaiting_new_color});
    }
  }

  std::sort(upload_candidates.begin(), upload_candidates.end(), [](const auto &a, const auto &b) {
    if (a.awaiting != b.awaiting)
      return a.awaiting > b.awaiting;
    if (a.awaiting)
      return a.lod > b.lod;
    return a.lod > b.lod;
  });

  auto upload_color = [&](gpu_node_buffer_t &rb, const render::loaded_node_data_t &ld)
  {
    callbacks.do_create_buffer(rb.render_buffers[1], points::render::buffer_type_vertex);
    bool should_normalize = (attr_min < attr_max) &&
                           !(ld.attribute_type == type_u16 && ld.attribute_components == components_3);
    if (should_normalize)
    {
      uint32_t normalized_size = 0;
      auto normalized_data = normalize_attribute_to_float(ld.attribute_data, ld.attribute_data_size,
                                                          ld.attribute_type, ld.attribute_components,
                                                          ld.point_count, attr_min, attr_max,
                                                          normalized_size);
      callbacks.do_initialize_buffer(rb.render_buffers[1], type_r32, ld.attribute_components, int(normalized_size), normalized_data.get());
    }
    else
    {
      callbacks.do_initialize_buffer(rb.render_buffers[1], ld.attribute_type, ld.attribute_components, int(ld.attribute_data_size), ld.attribute_data);
    }
  };

  int uploads_done = 0;
  for (auto &candidate : upload_candidates)
  {
    if (uploads_done >= max_uploads)
      break;
    if (gpu_memory_used >= upload_limit && !candidate.awaiting)
      break;
    auto &render_buffer = *render_buffers[candidate.index];

    auto loaded = node_loader->get_data(render_buffer.load_handle);
    render_buffer.load_handle = render::invalid_load_handle;

    if (render_buffer.awaiting_new_color)
    {
      render_buffer.draw_type = loaded.draw_type;
      upload_color(render_buffer, loaded);

      bool new_is_mono = (loaded.draw_type == render::dyn_points_1);
      render_buffer.params_data = glm::vec4(1.0f, 0.0f, render_buffer.old_color_is_mono ? 1.0f : 0.0f, new_is_mono ? 1.0f : 0.0f);
      if (!render_buffer.params_buffer.user_ptr)
      {
        callbacks.do_create_buffer(render_buffer.params_buffer, points::render::buffer_type_uniform);
        callbacks.do_initialize_buffer(render_buffer.params_buffer, type_r32, points::components_4, sizeof(render_buffer.params_data), &render_buffer.params_data);
      }
      else
      {
        callbacks.do_modify_buffer(render_buffer.params_buffer, 0, sizeof(render_buffer.params_data), &render_buffer.params_data);
      }

      render_buffer.old_color_memory = render_buffer.attribute_data_size;
      render_buffer.attribute_data_size = loaded.attribute_data_size;
      render_buffer.gpu_memory_size += loaded.attribute_data_size;

      render_buffer.crossfade_frame = 0;
      render_buffer.awaiting_new_color = false;

      gpu_memory_used += loaded.attribute_data_size;
      loaded.release();
      uploads_done++;
    }
    else
    {
      render_buffer.point_count = loaded.point_count;
      render_buffer.offset = loaded.offset;
      render_buffer.draw_type = loaded.draw_type;

      callbacks.do_create_buffer(render_buffer.render_buffers[0], points::render::buffer_type_vertex);
      callbacks.do_initialize_buffer(render_buffer.render_buffers[0], loaded.vertex_type, loaded.vertex_components, int(loaded.vertex_data_size), loaded.vertex_data);

      upload_color(render_buffer, loaded);

      render_buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(render_buffer.offset));

      callbacks.do_create_buffer(render_buffer.render_buffers[2], points::render::buffer_type_uniform);
      callbacks.do_initialize_buffer(render_buffer.render_buffers[2], type_r32, points::components_4x4, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

      render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
      render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
      render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
      render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
      render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
      render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;

      bool is_mono = (loaded.draw_type == render::dyn_points_1);
      render_buffer.params_data = glm::vec4(0.0f, 1.0f, 0.0f, is_mono ? 1.0f : 0.0f);
      callbacks.do_create_buffer(render_buffer.params_buffer, points::render::buffer_type_uniform);
      callbacks.do_initialize_buffer(render_buffer.params_buffer, type_r32, points::components_4, sizeof(render_buffer.params_data), &render_buffer.params_data);
      render_buffer.fade_frame = 0;

      size_t buf_mem = loaded.vertex_data_size + loaded.attribute_data_size + sizeof(render_buffer.camera_view);
      render_buffer.gpu_memory_size = buf_mem;
      render_buffer.attribute_data_size = loaded.attribute_data_size;
      gpu_memory_used += buf_mem;

      render_buffer.rendered = true;
      loaded.release();
      uploads_done++;
    }
  }
  return uploads_done;
}

void gpu_buffer_manager_t::schedule_io(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                       const frame_node_registry_t &registry,
                                       const selection_result_t &selection,
                                       const tree_config_t &tree_config,
                                       std::unique_ptr<render::node_data_loader_t> &node_loader,
                                       int max_requests)
{
  m_frontier.clear();

  auto needs_load = [](const gpu_node_buffer_t &rb) {
    return rb.load_handle == render::invalid_load_handle && (!rb.rendered || rb.awaiting_new_color);
  };

  // Active nodes
  for (auto &node_id : selection.active_set)
  {
    auto *node = registry.get_node(node_id);
    if (!node)
      continue;
    for (int idx : node->buffer_indices)
    {
      auto &rb = *render_buffers[idx];
      if (needs_load(rb))
        m_frontier.push_back({idx, rb.node_info.lod, rb.awaiting_new_color});
    }
    // One-level lookahead: children of active nodes
    for (auto &child_id : node->children)
    {
      auto *child = registry.get_node(child_id);
      if (!child)
        continue;
      for (int idx : child->buffer_indices)
      {
        auto &rb = *render_buffers[idx];
        if (needs_load(rb))
          m_frontier.push_back({idx, rb.node_info.lod, rb.awaiting_new_color});
      }
    }
  }

  // Second pass: ensure all awaiting_new_color nodes get IO scheduled,
  // even if they are not in the active set
  for (int idx = 0; idx < int(render_buffers.size()); idx++)
  {
    auto &rb = *render_buffers[idx];
    if (rb.awaiting_new_color && needs_load(rb))
      m_frontier.push_back({idx, rb.node_info.lod, true});
  }

  // Awaiting nodes first (root-to-leaf), then normal frontier nodes (deepest-first)
  std::sort(m_frontier.begin(), m_frontier.end(), [](const auto &a, const auto &b) {
    if (a.awaiting != b.awaiting)
      return a.awaiting > b.awaiting;
    if (a.awaiting)
      return a.lod > b.lod;
    return a.lod > b.lod;
  });

  std::unordered_set<int> started;
  int requests_started = 0;
  for (auto &fc : m_frontier)
  {
    if (requests_started >= max_requests)
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

void gpu_buffer_manager_t::evict(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                  const frame_node_registry_t &registry,
                                  const selection_result_t &selection,
                                  const glm::dvec3 &camera_position,
                                  size_t &gpu_memory_used,
                                  size_t target_memory,
                                  render::callback_manager_t &callbacks,
                                  std::unique_ptr<render::node_data_loader_t> &node_loader)
{
  if (gpu_memory_used <= target_memory)
    return;

  // Build non-evictable set: active nodes + ancestors
  m_non_evictable.clear();
  for (auto &node_id : selection.active_set)
  {
    m_non_evictable.insert(node_id);
    node_id_t current = node_id;
    while (true)
    {
      auto *node = registry.get_node(current);
      if (!node)
        break;
      auto parent = node->parent_id;
      if (!registry.get_node(parent))
        break;
      if (!m_non_evictable.insert(parent).second)
        break;
      current = parent;
    }
  }

  m_evictable.clear();
  for (auto &[node_id, node] : registry.nodes())
  {
    if (m_non_evictable.count(node_id))
      continue;
    if (!node.all_rendered)
      continue;
    glm::dvec3 center = (node.tight_aabb.min + node.tight_aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    m_evictable.push_back({node_id, dist});
  }
  std::sort(m_evictable.begin(), m_evictable.end(), [](const evictable_node_t &a, const evictable_node_t &b) { return a.distance > b.distance; });

  for (auto &ev : m_evictable)
  {
    if (gpu_memory_used <= target_memory)
      break;
    auto *node = registry.get_node(ev.node_id);
    if (!node)
      continue;
    bool has_awaiting = false;
    for (int idx : node->buffer_indices)
    {
      if (render_buffers[idx]->awaiting_new_color)
      {
        has_awaiting = true;
        break;
      }
    }
    if (has_awaiting)
      continue;
    for (int idx : node->buffer_indices)
      destroy_gpu_buffer(*render_buffers[idx], callbacks, node_loader, gpu_memory_used);
  }
}

void gpu_buffer_manager_t::handle_attribute_change(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                                                    render::callback_manager_t &callbacks,
                                                    std::unique_ptr<render::node_data_loader_t> &node_loader,
                                                    size_t &gpu_memory_used)
{
  for (auto &rb : render_buffers)
  {
    auto &buf = *rb;
    if (buf.load_handle != render::invalid_load_handle)
    {
      node_loader->cancel(buf.load_handle);
      buf.load_handle = render::invalid_load_handle;
    }
    if (buf.rendered)
    {
      if (buf.awaiting_new_color)
      {
        // Already awaiting — render_buffers[1] is empty.
        // Keep old_color_buffer as-is (it still has the last visible color).
      }
      else
      {
        if (buf.old_color_valid)
        {
          callbacks.do_destroy_buffer(buf.old_color_buffer);
          gpu_memory_used -= buf.old_color_memory;
          buf.gpu_memory_size -= buf.old_color_memory;
        }
        buf.old_color_buffer = buf.render_buffers[1];
        buf.render_buffers[1] = {};
        buf.old_color_valid = true;
        buf.old_color_is_mono = (buf.draw_type == render::dyn_points_1);
        buf.old_color_memory = buf.attribute_data_size;
        buf.awaiting_new_color = true;
        buf.crossfade_frame = 0;
      }
    }
    else
    {
      destroy_gpu_buffer(buf, callbacks, node_loader, gpu_memory_used);
    }
  }
}

} // namespace points::converter
