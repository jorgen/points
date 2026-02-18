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
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>

#include <algorithm>
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

static void add_request_to_dynbuffer(storage_handler_t &storage_handler, dyn_points_draw_buffer_t &buffer, const tree_walker_data_t &node)
{
  buffer.node_info = node;
  buffer.data_handler = std::make_shared<dyn_points_data_handler_t>(node.format);
  buffer.data_handler->start_requests(buffer.data_handler, storage_handler, node.locations);
}

void converter_data_source_t::destroy_gpu_buffer(dyn_points_draw_buffer_t &buf)
{
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

  std::vector<std::unique_ptr<dyn_points_draw_buffer_t>> new_render_buffers;
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
      auto &new_buffer = new_render_buffers.emplace_back(new dyn_points_draw_buffer_t());
      add_request_to_dynbuffer(processor.storage_handler(), *new_buffer, node);
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
    auto &new_buffer = new_render_buffers.emplace_back(new dyn_points_draw_buffer_t());
    add_request_to_dynbuffer(processor.storage_handler(), *new_buffer, node);
  }

  render_buffers = std::move(new_render_buffers);

  // Initialize GPU data for buffers that have finished loading
  auto tree_config = processor.tree_config();
  for (auto &render_buffer_ptr : render_buffers)
  {
    assert(render_buffer_ptr);
    auto &render_buffer = *render_buffer_ptr;
    if (render_buffer.data_handler)
    {
      if (render_buffer.data_handler->is_done())
      {
        render_buffer.point_count = render_buffer.data_handler->header.point_count;
        convert_points_to_vertex_data(tree_config, *render_buffer.data_handler, render_buffer);
        callbacks.do_create_buffer(render_buffer.render_buffers[0], points::render::buffer_type_vertex);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[0], render_buffer.format[0].type, render_buffer.format[0].components, int(render_buffer.data_info[0].size), render_buffer.data_info[0].data);

        convert_attribute_to_draw_buffer_data(*render_buffer.data_handler, render_buffer, 1);
        callbacks.do_create_buffer(render_buffer.render_buffers[1], points::render::buffer_type_vertex);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[1], render_buffer.format[1].type, render_buffer.format[1].components, int(render_buffer.data_info[1].size), render_buffer.data_info[1].data);

        render_buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(render_buffer.offset));

        callbacks.do_create_buffer(render_buffer.render_buffers[2], points::render::buffer_type_uniform);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[2], type_r32, points::components_4x4, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

        render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
        render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
        render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
        render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
        render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
        render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;

        size_t buf_mem = render_buffer.data_info[0].size + render_buffer.data_info[1].size + sizeof(render_buffer.camera_view);
        render_buffer.gpu_memory_size = buf_mem;
        gpu_memory_used += buf_mem;

        render_buffer.rendered = true;
        render_buffer.data_handler.reset();
      }
    }
  }

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
  for (auto &[node_id, group] : node_map)
  {
    if (!node_map.count(group.parent_id))
      active_set.insert(node_id);
  }

  // Iteratively replace parents with children when all children are GPU-ready
  bool changed = true;
  while (changed)
  {
    changed = false;
    std::vector<node_id_t> to_remove;
    std::vector<node_id_t> to_add;
    for (auto &node_id : active_set)
    {
      auto children_it = children_map.find(node_id);
      if (children_it == children_map.end())
        continue;
      bool all_children_ready = true;
      for (auto &child_id : children_it->second)
      {
        if (!node_map[child_id].all_rendered)
        {
          all_children_ready = false;
          break;
        }
      }
      if (all_children_ready)
      {
        to_remove.push_back(node_id);
        for (auto &child_id : children_it->second)
          to_add.push_back(child_id);
        changed = true;
      }
    }
    for (auto &id : to_remove)
      active_set.erase(id);
    for (auto &id : to_add)
      active_set.insert(id);
  }

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
    glm::dvec3 center = (group.aabb.min + group.aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    sorted_active.push_back({node_id, dist, group.total_gpu_memory, group.frustum_visible});
  }
  std::sort(sorted_active.begin(), sorted_active.end(), [](const active_node_info_t &a, const active_node_info_t &b) { return a.distance < b.distance; });

  // Phase 4: GPU memory budget cutoff (nearest first)
  std::unordered_set<node_id_t, node_id_hash, node_id_equal> render_set;
  size_t frame_memory = 0;
  for (auto &info : sorted_active)
  {
    if (mem_budget > 0 && frame_memory + info.gpu_memory > mem_budget)
      break;
    frame_memory += info.gpu_memory;
    render_set.insert(info.node_id);
  }

  // Phase 5: Emit draw commands for active nodes within budget and frustum
  for (auto &info : sorted_active)
  {
    if (!render_set.count(info.node_id))
      continue;
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
      render::draw_group_t draw_group = {render_buffer.draw_type, render_buffer.render_list, 3, int(render_buffer.point_count)};
      to_render_add_render_group(to_render, draw_group);
    }
  }

  // Phase 6: Distance-based eviction with parent preservation
  // Build non-evictable set: active nodes + ancestors + direct children of active nodes
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
    // Mark direct children as non-evictable (needed to eventually replace parent)
    auto children_it = children_map.find(node_id);
    if (children_it != children_map.end())
    {
      for (auto &child_id : children_it->second)
        non_evictable.insert(child_id);
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
    glm::dvec3 center = (group.aabb.min + group.aabb.max) * 0.5;
    double dist = glm::length(center - camera_position);
    evictable.push_back({node_id, dist});
  }
  std::sort(evictable.begin(), evictable.end(), [](const evictable_node_t &a, const evictable_node_t &b) { return a.distance > b.distance; });

  for (auto &ev : evictable)
  {
    if (gpu_memory_used <= mem_budget)
      break;
    auto &group = node_map[ev.node_id];
    for (int idx : group.buffer_indices)
      destroy_gpu_buffer(*render_buffers[idx]);
  }

  // Phase 7: Dynamic pixel error threshold adjustment
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (gpu_memory_used > mem_budget * 9 / 10)
      effective_pixel_error_threshold *= 1.1;
    else if (gpu_memory_used < mem_budget / 2 && effective_pixel_error_threshold > pix_threshold)
      effective_pixel_error_threshold = std::max(pix_threshold, effective_pixel_error_threshold * 0.9);
    effective_pixel_error_threshold = std::clamp(effective_pixel_error_threshold, 1.0, 1000.0);
  }
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

} // namespace points::converter
