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
#include "render_pipeline.hpp"

#include "data_source.hpp"
#include "native_node_data_loader.hpp"
#include "renderer.hpp"

#include <vio/thread_pool.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace points::converter
{

// Estimate the world-space spacing between adjacent points in a node, for adaptive splat sizing, from the
// node's tight bounds + point count (no assumption of a fixed subsample grid).
//
// Point clouds are overwhelmingly SURFACES (terrain, walls, scanned objects), so we estimate spacing as if
// the N points tile the node's dominant face -- its two LARGEST extents -- and deliberately ignore the
// smallest extent. That is what keeps a surface's relief/thickness, and the axis-aligned-box slack of a
// tilted plane, from inflating the splat: folding in the full box volume would swing the size several-fold
// with pure orientation and over-cover every planar node whose relief is thicker than its point spacing
// (i.e. all real terrain). A 1D term (largest/N) keeps thin, edge-like nodes from collapsing to
// sub-spacing dots. Genuinely volumetric clouds are then slightly under-covered, which the size multiplier
// absorbs -- the safe direction (crisper dots, not the gaps this feature exists to remove).
static float node_world_spacing(const node_aabb_t &tight, uint64_t point_count)
{
  if (point_count < 1)
    return 0.0f;
  const double n = double(point_count);
  const glm::dvec3 e = glm::abs(tight.max - tight.min);
  const double largest = std::max({e.x, e.y, e.z});
  const double smallest = std::min({e.x, e.y, e.z});
  const double middle = (e.x + e.y + e.z) - largest - smallest;                    // remaining (middle) extent
  const double surface = (largest * middle > 0.0) ? std::sqrt(largest * middle / n) : 0.0; // 2D: dominant face
  const double line = (largest > 0.0) ? largest / n : 0.0;                         // 1D: thin edge-like node
  return float(std::max(surface, line));
}

bool render_node_less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs)
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

void destroy_render_node(render_node_t &node, render::callback_manager_t &callbacks, render::node_data_loader_t *node_loader)
{
  // If a worker thread is converting this node, we must wait for it to finish
  // before we can safely touch the node's data.
  if (node.io_state == render_node_io_state::converting)
  {
    while (!node.convert_done.load(std::memory_order_acquire))
      std::this_thread::yield();
  }
  if (node.load_handle != render::invalid_load_handle)
  {
    node_loader->cancel(node.load_handle);
    node.load_handle = render::invalid_load_handle;
  }
  node.loaded_data.release();
  if (node.gpu_state == render_node_gpu_state::uploaded)
  {
    for (auto &buf : node.gpu_buffers)
    {
      if (buf.user_ptr)
        callbacks.do_destroy_buffer(buf);
    }
  }
  if (node.params_buffer.user_ptr)
    callbacks.do_destroy_buffer(node.params_buffer);
  node.gpu_state = render_node_gpu_state::none;
  node.io_state = render_node_io_state::none;
}

render_list_t build_render_list(
    const std::vector<tree_walker_data_t> &walker_nodes,
    render_list_t &&previous_list,
    float fade_duration_ms,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader)
{
  render_list_t new_list;
  new_list.reserve(walker_nodes.size() + 16);

  auto wit = walker_nodes.begin();
  auto pit = previous_list.begin();

  while (wit != walker_nodes.end() && pit != previous_list.end())
  {
    auto &pnode = **pit;
    if (render_node_less_than(*wit, pnode.walker_data))
    {
      auto node = std::make_unique<render_node_t>();
      node->walker_data = *wit;
      node->fade_state = render_node_fade_state::fade_in;
      node->fade_ms = 0.0f;
      new_list.push_back(std::move(node));
      ++wit;
    }
    else if (render_node_less_than(pnode.walker_data, *wit))
    {
      if (pnode.fade_state != render_node_fade_state::fade_out)
      {
        pnode.fade_state = render_node_fade_state::fade_out;
        pnode.fade_ms = 0.0f;
      }
      if (pnode.gpu_state == render_node_gpu_state::uploaded && pnode.fade_ms < fade_duration_ms)
      {
        new_list.push_back(std::move(*pit));
      }
      else
      {
        destroy_render_node(pnode, callbacks, node_loader);
      }
      ++pit;
    }
    else
    {
      pnode.walker_data.frustum_visible = wit->frustum_visible;
      std::memcpy(pnode.walker_data.format, wit->format, sizeof(wit->format));
      std::memcpy(pnode.walker_data.locations, wit->locations, sizeof(wit->locations));
      pnode.walker_data.point_count = wit->point_count;
      pnode.walker_data.node_point_count = wit->node_point_count; // keep paired with tight_aabb (below)
      pnode.walker_data.aabb = wit->aabb;
      pnode.walker_data.tight_aabb = wit->tight_aabb;
      if (pnode.fade_state == render_node_fade_state::fade_out)
      {
        pnode.fade_state = render_node_fade_state::fade_in;
        pnode.fade_ms = 0.0f;
      }
      new_list.push_back(std::move(*pit));
      ++wit;
      ++pit;
    }
  }

  while (wit != walker_nodes.end())
  {
    auto node = std::make_unique<render_node_t>();
    node->walker_data = *wit;
    node->fade_state = render_node_fade_state::fade_in;
    node->fade_ms = 0.0f;
    new_list.push_back(std::move(node));
    ++wit;
  }

  while (pit != previous_list.end())
  {
    auto &pnode = **pit;
    if (pnode.fade_state != render_node_fade_state::fade_out)
    {
      pnode.fade_state = render_node_fade_state::fade_out;
      pnode.fade_ms = 0.0f;
    }
    if (pnode.gpu_state == render_node_gpu_state::uploaded && pnode.fade_ms < fade_duration_ms)
    {
      new_list.push_back(std::move(*pit));
    }
    else
    {
      destroy_render_node(pnode, callbacks, node_loader);
    }
    ++pit;
  }

  return new_list;
}

static std::shared_ptr<uint8_t[]> normalize_attribute_to_float(const void *data, uint32_t data_size, points_type_t type, points_components_t components,
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
  case points_type_u8: case points_type_i8: type_size = 1; break;
  case points_type_u16: case points_type_i16: type_size = 2; break;
  case points_type_u32: case points_type_i32: case points_type_r32: type_size = 4; break;
  case points_type_u64: case points_type_i64: case points_type_r64: type_size = 8; break;
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
      case points_type_u8:  { uint8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case points_type_i8:  { int8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case points_type_u16: { uint16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case points_type_i16: { int16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case points_type_u32: { uint32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case points_type_i32: { int32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case points_type_r32: { float v; memcpy(&v, elem, 4); val = double(v); break; }
      case points_type_u64: { uint64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case points_type_i64: { int64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case points_type_r64: { double v; memcpy(&v, elem, 8); val = v; break; }
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

// Fetch decompressed data from the loader and store on the node.
// This does morton decode + attribute extraction (CPU-heavy).
// Runs on a worker thread — signals completion via node.convert_done atomic.
static void convert_node_data(render_node_t &node, render::node_data_loader_t *node_loader)
{
  node.loaded_data = node_loader->get_data(node.load_handle);
  node.load_handle = render::invalid_load_handle;

  if (!node.loaded_data.vertex_data || node.loaded_data.point_count == 0)
  {
    node.loaded_data.release();
  }
  else
  {
    node.point_count = node.loaded_data.point_count;
    node.offset = node.loaded_data.offset;
    node.draw_type = node.loaded_data.draw_type;
    node.gpu_memory_size = node.loaded_data.vertex_data_size + node.loaded_data.attribute_data_size
                          + sizeof(node.camera_view) + sizeof(node.params_data);
  }
  node.convert_done.store(true, std::memory_order_release);
}

io_upload_stats_t process_io_and_upload(
    render_list_t &render_list,
    const glm::dvec3 &camera_position,
    const tree_config_t &tree_config,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader,
    vio::thread_pool_t &convert_pool,
    const render::frame_camera_cpp_t &camera_frame,
    int max_concurrent_io,
    int max_new_io_per_frame,
    size_t max_upload_bytes,
    size_t gpu_memory_budget,
    double attr_min, double attr_max)
{
  using clock = std::chrono::high_resolution_clock;
  auto to_ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };

  io_upload_stats_t stats;
  std::vector<priority_entry_t> load_list;
  std::vector<priority_entry_t> upload_list;

  // Phase A: Single pass — compute distances, advance state machine, classify nodes
  auto t0 = clock::now();
  for (int i = 0; i < int(render_list.size()); i++)
  {
    auto &node = *render_list[i];
    glm::dvec3 center = (node.walker_data.aabb.min + node.walker_data.aabb.max) * 0.5;
    node.cached_distance = glm::length(center - camera_position);

    if (node.gpu_state == render_node_gpu_state::uploaded)
      stats.gpu_memory_used += node.gpu_memory_size;

    switch (node.io_state)
    {
    case render_node_io_state::loading:
      if (node.load_handle != render::invalid_load_handle && node_loader->is_ready(node.load_handle))
      {
        node.io_state = render_node_io_state::converting;
        node.convert_done.store(false, std::memory_order_relaxed);
        convert_pool.enqueue([&node, node_loader] { convert_node_data(node, node_loader); });
      }
      else
      {
        stats.io_in_flight++;
      }
      break;
    case render_node_io_state::converting:
      if (node.convert_done.load(std::memory_order_acquire))
      {
        if (node.loaded_data.vertex_data && node.loaded_data.point_count > 0)
        {
          node.io_state = render_node_io_state::loaded;
          upload_list.push_back({i, node.cached_distance});
        }
        else
        {
          node.io_state = render_node_io_state::none;
        }
      }
      break;
    case render_node_io_state::loaded:
      if (node.gpu_state == render_node_gpu_state::none)
        upload_list.push_back({i, node.cached_distance});
      break;
    case render_node_io_state::none:
      if (node.gpu_state == render_node_gpu_state::none && node.fade_state != render_node_fade_state::fade_out)
        load_list.push_back({i, node.cached_distance});
      break;
    }
  }
  auto t1 = clock::now();
  stats.scan_classify_ms = to_ms(t1 - t0);

  // Phase B: Schedule IO — sort by distance, issue closest first
  auto dist_cmp = [](const priority_entry_t &a, const priority_entry_t &b) { return a.distance < b.distance; };
  std::sort(load_list.begin(), load_list.end(), dist_cmp);

  for (auto &entry : load_list)
  {
    if (stats.io_in_flight >= max_concurrent_io)
      break;
    if (stats.io_scheduled >= max_new_io_per_frame)
      break;
    auto &node = *render_list[entry.index];
    native_load_request_t req;
    std::memcpy(req.format, node.walker_data.format, sizeof(req.format));
    std::memcpy(req.locations, node.walker_data.locations, sizeof(req.locations));
    req.tree_config = tree_config;
    node.load_handle = node_loader->request_load(&req, sizeof(req));
    node.io_state = render_node_io_state::loading;
    stats.io_in_flight++;
    stats.io_scheduled++;
  }
  auto t2 = clock::now();
  stats.schedule_io_ms = to_ms(t2 - t1);

  // Phase C: Upload to GPU — sort by distance, normalize then transfer
  std::sort(upload_list.begin(), upload_list.end(), dist_cmp);
  size_t bytes_uploaded = 0;
  double normalize_total = 0;
  double gpu_transfer_total = 0;

  for (auto &entry : upload_list)
  {
    if (bytes_uploaded >= max_upload_bytes)
      break;
    auto &node = *render_list[entry.index];
    if (stats.gpu_memory_used + node.gpu_memory_size > gpu_memory_budget)
      break;

    auto &loaded = node.loaded_data;

    // Normalize attribute if needed (CPU work)
    auto tn0 = clock::now();
    std::shared_ptr<uint8_t[]> normalized_data;
    uint32_t normalized_size = 0;
    bool should_normalize = (attr_min < attr_max) &&
                           !(loaded.attribute_type == points_type_u16 && loaded.attribute_components == points_components_3);
    if (should_normalize)
    {
      normalized_data = normalize_attribute_to_float(loaded.attribute_data, loaded.attribute_data_size,
                                                      loaded.attribute_type, loaded.attribute_components,
                                                      loaded.point_count, attr_min, attr_max,
                                                      normalized_size);
    }
    auto tn1 = clock::now();
    normalize_total += to_ms(tn1 - tn0);

    // GPU buffer creation + transfer
    auto tg0 = clock::now();
    callbacks.do_create_buffer(node.gpu_buffers[0], points_buffer_type_vertex);
    callbacks.do_initialize_buffer(node.gpu_buffers[0], loaded.vertex_type, loaded.vertex_components, int(loaded.vertex_data_size), loaded.vertex_data);

    callbacks.do_create_buffer(node.gpu_buffers[1], points_buffer_type_vertex);
    if (should_normalize)
      callbacks.do_initialize_buffer(node.gpu_buffers[1], points_type_r32, loaded.attribute_components, int(normalized_size), normalized_data.get());
    else
      callbacks.do_initialize_buffer(node.gpu_buffers[1], loaded.attribute_type, loaded.attribute_components, int(loaded.attribute_data_size), loaded.attribute_data);

    auto offset = to_glm(tree_config.offset) + to_glm(node.offset);
    node.camera_view = glm::mat4(camera_frame.projection * glm::translate(camera_frame.view, offset));
    callbacks.do_create_buffer(node.gpu_buffers[2], points_buffer_type_uniform);
    callbacks.do_initialize_buffer(node.gpu_buffers[2], points_type_r32, points_components_4x4, sizeof(node.camera_view), &node.camera_view);

    bool is_mono = (loaded.draw_type == points_dyn_points_1);
    node.params_data = glm::vec4(0.0f, 1.0f, 0.0f, is_mono ? 1.0f : 0.0f);
    callbacks.do_create_buffer(node.params_buffer, points_buffer_type_uniform);
    callbacks.do_initialize_buffer(node.params_buffer, points_type_r32, points_components_4, sizeof(node.params_data), &node.params_data);

    node.gpu_state = render_node_gpu_state::uploaded;
    node.loaded_data.release();
    auto tg1 = clock::now();
    gpu_transfer_total += to_ms(tg1 - tg0);

    bytes_uploaded += node.gpu_memory_size;
    stats.gpu_memory_used += node.gpu_memory_size;
    stats.uploads_done++;
  }

  stats.normalize_ms = normalize_total;
  stats.gpu_upload_ms = gpu_transfer_total;

  return stats;
}

void update_fades(
    render_list_t &render_list,
    float delta_ms,
    float fade_duration_ms)
{
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.gpu_state != render_node_gpu_state::uploaded)
      continue;

    node.fade_ms += delta_ms;

    if (node.fade_state == render_node_fade_state::fade_in)
    {
      if (node.fade_ms >= fade_duration_ms)
      {
        node.fade_ms = fade_duration_ms;
        node.fade_state = render_node_fade_state::steady;
      }
    }
  }
}

int emit_draws(
    render_list_t &render_list,
    render::callback_manager_t &callbacks,
    const render::frame_camera_cpp_t &camera,
    const tree_config_t &tree_config,
    points_to_render_t *to_render,
    float fade_duration_ms,
    uint64_t &points_rendered)
{
  int nodes_drawn = 0;
  points_rendered = 0;

  // Pass 1: steady opaque nodes
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.gpu_state != render_node_gpu_state::uploaded)
      continue;
    if (!node.walker_data.frustum_visible)
      continue;
    if (node.fade_state != render_node_fade_state::steady)
      continue;

    auto offset = to_glm(tree_config.offset) + to_glm(node.offset);
    node.camera_view = glm::mat4(camera.projection * glm::translate(camera.view, offset));
    callbacks.do_modify_buffer(node.gpu_buffers[2], 0, sizeof(node.camera_view), &node.camera_view);

    if (node.params_buffer.user_ptr)
      callbacks.do_destroy_buffer(node.params_buffer);

    node.draw_list[0] = {points_dyn_points_bm_vertex, node.gpu_buffers[0].user_ptr};
    node.draw_list[1] = {points_dyn_points_bm_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[2] = {points_dyn_points_bm_camera, node.gpu_buffers[2].user_ptr};

    points_draw_group_t draw_group = {node.draw_type, node.draw_list, 3, int(node.point_count), node.walker_data.lod,
                                      node_world_spacing(node.walker_data.tight_aabb, node.walker_data.node_point_count ? node.walker_data.node_point_count : node.point_count)};
    points_to_render_add_render_group(to_render, draw_group);

    points_rendered += node.point_count;
    nodes_drawn++;
  }

  // Pass 2: fading nodes
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.gpu_state != render_node_gpu_state::uploaded)
      continue;
    if (node.fade_state == render_node_fade_state::steady)
      continue;
    if (!node.walker_data.frustum_visible && node.fade_state != render_node_fade_state::fade_out)
      continue;

    float alpha;
    if (node.fade_state == render_node_fade_state::fade_in)
      alpha = node.fade_ms / fade_duration_ms;
    else
      alpha = 1.0f - node.fade_ms / fade_duration_ms;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    if (alpha <= 0.0f)
      continue;

    auto offset = to_glm(tree_config.offset) + to_glm(node.offset);
    node.camera_view = glm::mat4(camera.projection * glm::translate(camera.view, offset));
    callbacks.do_modify_buffer(node.gpu_buffers[2], 0, sizeof(node.camera_view), &node.camera_view);

    bool is_mono = (node.draw_type == points_dyn_points_1);
    node.params_data = glm::vec4(alpha, 1.0f, is_mono ? 1.0f : 0.0f, is_mono ? 1.0f : 0.0f);

    if (!node.params_buffer.user_ptr)
    {
      callbacks.do_create_buffer(node.params_buffer, points_buffer_type_uniform);
      callbacks.do_initialize_buffer(node.params_buffer, points_type_r32, points_components_4, sizeof(node.params_data), &node.params_data);
    }
    else
    {
      callbacks.do_modify_buffer(node.params_buffer, 0, sizeof(node.params_data), &node.params_data);
    }

    node.draw_list[0] = {points_dyn_points_bm_vertex, node.gpu_buffers[0].user_ptr};
    node.draw_list[1] = {points_dyn_points_bm_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[2] = {points_dyn_points_bm_camera, node.gpu_buffers[2].user_ptr};
    node.draw_list[3] = {points_dyn_points_bm_old_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[4] = {points_dyn_points_bm_params, node.params_buffer.user_ptr};

    points_draw_group_t draw_group = {points_dyn_points_crossfade, node.draw_list, 5, int(node.point_count), node.walker_data.lod,
                                      node_world_spacing(node.walker_data.tight_aabb, node.walker_data.node_point_count ? node.walker_data.node_point_count : node.point_count)};
    points_to_render_add_render_group(to_render, draw_group);

    points_rendered += node.point_count;
    nodes_drawn++;
  }

  return nodes_drawn;
}

void handle_attribute_change(
    render_list_t &render_list,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader)
{
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.load_handle != render::invalid_load_handle)
    {
      node_loader->cancel(node.load_handle);
      node.load_handle = render::invalid_load_handle;
    }
    node.loaded_data.release();
    if (node.gpu_state == render_node_gpu_state::uploaded)
    {
      if (node.gpu_buffers[1].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[1]);
      if (node.gpu_buffers[0].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[0]);
      if (node.gpu_buffers[2].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[2]);
      if (node.params_buffer.user_ptr)
        callbacks.do_destroy_buffer(node.params_buffer);
      node.gpu_state = render_node_gpu_state::none;
    }
    node.io_state = render_node_io_state::none;
  }
}

} // namespace points::converter
