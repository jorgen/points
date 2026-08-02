/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#include "render_pipeline.hpp"

#include "data_source.hpp"
#include "memory_budget.hpp" // estimate_node_cpu_bytes / estimate_node_gpu_bytes (byte-gated IO)
#include "native_node_data_loader.hpp" // loaded_node_impl_data_t (salvage data_handler on promotion)
#include "renderer.hpp"
#include "virtual_tree.hpp" // destroy_virtual_subtree

#include <vio/thread_pool.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace dew::converter
{

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

void destroy_render_node(render_node_t &node, render::callback_manager_t &callbacks, render::node_data_loader_t *node_loader, size_t *virtual_gpu_used)
{
  // If a worker thread is converting this node, we must wait for it to finish
  // before we can safely touch the node's data.
  if (node.io_state == render_node_io_state::converting)
  {
    while (!node.convert_done.load(std::memory_order_acquire))
      std::this_thread::yield();
  }
  // R11: a resident-build job may be in flight writing node.pending_resident; wait before freeing the node.
  if (node.resident_building)
  {
    while (!node.resident_ready.load(std::memory_order_acquire))
      std::this_thread::yield();
    node.resident_building = false;
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
  // Tear down any virtual subtree children-first (spin-waits in-flight materialize) before freeing the resident
  // it reads from. Ordering guarantees no materialize job dereferences a freed resident_source.
  if (node.virtual_root)
    destroy_virtual_subtree(node.virtual_root, callbacks, virtual_gpu_used);
  node.resident.reset();
  node.resident_handler.reset();
  node.is_virtual_source = false;
  node.draw_suppressed = false;
  node.gpu_state = render_node_gpu_state::none;
  node.io_state = render_node_io_state::none;
}

bool node_is_busy(const render_node_t &node)
{
  if (node.io_state == render_node_io_state::converting && !node.convert_done.load(std::memory_order_acquire))
    return true;
  if (node.resident_building && !node.resident_ready.load(std::memory_order_acquire))
    return true;
  if (node.virtual_root && virtual_subtree_has_inflight(*node.virtual_root))
    return true;
  return false;
}

render_list_t build_render_list(
    const std::vector<tree_walker_data_t> &walker_nodes,
    render_list_t &&previous_list,
    float fade_duration_ms,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader,
    size_t *virtual_gpu_used,
    render_list_t &deferred_destroy)
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
      else if (node_is_busy(pnode))
      {
        // A worker is still decoding this departed node; destroying it now would spin-wait the main thread.
        // Defer it -- the data source drains pending_destroy each frame once the job finishes.
        deferred_destroy.push_back(std::move(*pit));
      }
      else
      {
        destroy_render_node(pnode, callbacks, node_loader, virtual_gpu_used);
      }
      ++pit;
    }
    else
    {
      pnode.walker_data.frustum_visible = wit->frustum_visible;
      std::memcpy(pnode.walker_data.format, wit->format, sizeof(wit->format));
      std::memcpy(pnode.walker_data.locations, wit->locations, sizeof(wit->locations));
      pnode.walker_data.point_count = wit->point_count;
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
    else if (node_is_busy(pnode))
    {
      deferred_destroy.push_back(std::move(*pit)); // still decoding: defer (see above)
    }
    else
    {
      destroy_render_node(pnode, callbacks, node_loader, virtual_gpu_used);
    }
    ++pit;
  }

  return new_list;
}

// normalize_attribute_to_float moved to point_buffer_render_helper.hpp (shared with the virtual-node upload).

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
    node.prefix_count = node.loaded_data.prefix_count; // survives loaded_data.release() after upload
    node.has_lod_order = node.loaded_data.has_lod_order;
    node.gpu_memory_size = node.loaded_data.vertex_data_size + node.loaded_data.attribute_data_size
                          + node.loaded_data.rep_level_data_size
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
    const io_limits_t &limits,
    double attr_min, double attr_max,
    bool promote_leaves,
    size_t virtual_gpu_used,
    std::vector<render::loaded_node_data_t> *reap_sink)
{
  using clock = std::chrono::high_resolution_clock;
  auto to_ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };

  io_upload_stats_t stats;
  // Departed-but-busy nodes parked outside the render list still pin decoded CPU buffers in the same heap.
  stats.backlog_bytes = limits.deferred_backlog_bytes;
  std::vector<priority_entry_t> load_list;
  std::vector<priority_entry_t> upload_list;

  // Phase A: Single pass — compute distances, advance state machine, classify nodes
  auto t0 = clock::now();
  for (int i = 0; i < int(render_list.size()); i++)
  {
    auto &node = *render_list[i];
    // Distance to the NEAREST point of the node's TIGHT (actual-points) AABB. With per-point LOD in the shader
    // this is no longer the visible-density lever -- it only (a) bounds how many points we submit
    // (draw_size = prefix down to the finest level any part of the node needs) and (b) prioritizes IO
    // closest-first. Nearest guarantees the fine near points are submitted; the shader culls the far ones.
    const auto &taabb = node.walker_data.tight_aabb;
    glm::dvec3 nearest = glm::clamp(camera_position, taabb.min, taabb.max);
    node.cached_distance = glm::length(nearest - camera_position);

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
      stats.backlog_bytes += estimate_node_cpu_bytes(node.walker_data);
      stats.projected_gpu_bytes += estimate_node_gpu_bytes(node.walker_data);
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
      stats.backlog_bytes += estimate_node_cpu_bytes(node.walker_data);
      stats.projected_gpu_bytes += estimate_node_gpu_bytes(node.walker_data);
      break;
    case render_node_io_state::loaded:
      // Charge ONLY while awaiting upload: a steady-state node keeps io_state==loaded after upload, but its
      // decoded buffers were reaped then (loaded_data.release() nulls pointers, not the sizes) and its GPU
      // bytes are already in gpu_memory_used above -- charging here too would permanently starve new IO.
      if (node.gpu_state == render_node_gpu_state::none)
      {
        upload_list.push_back({i, node.cached_distance});
        // Decoded outputs are exact now; the decode inputs are still alive via _impl_data's data_handler
        // until the post-upload reap, so keep charging the estimate for them.
        stats.backlog_bytes += node.loaded_data.vertex_data_size + node.loaded_data.attribute_data_size + node.loaded_data.rep_level_data_size + estimate_node_input_bytes(node.walker_data);
        stats.projected_gpu_bytes += node.gpu_memory_size;
      }
      break;
    case render_node_io_state::none:
      // monolith_freed: a live virtual cut represents this leaf; don't reload its monolith (R3). On un-promotion
      // the promoter clears monolith_freed + io_state so it reloads here.
      if (node.gpu_state == render_node_gpu_state::none && node.fade_state != render_node_fade_state::fade_out && !node.monolith_freed)
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
    if (stats.io_in_flight >= limits.max_concurrent_io)
      break;
    if (stats.io_scheduled >= limits.max_new_io_per_frame)
      break;
    auto &node = *render_list[entry.index];
    // Byte gates (closest-first, so `break` like the upload loop -- a far node must not leapfrog a near one).
    // The backlog gate is what bounds CPU-heap growth: without it, every decoded node frees an IO slot while
    // its buffers wait (possibly forever, if the GPU budget is full) in the same heap. The GPU-fit gate skips
    // loads the upload loop could not accept anyway, so nothing is decoded just to stall.
    const uint64_t est_cpu = estimate_node_cpu_bytes(node.walker_data);
    const uint64_t est_gpu = estimate_node_gpu_bytes(node.walker_data);
    // Escape hatch: always admit the closest node when nothing is in flight or awaiting upload. A single
    // node whose estimate exceeds the cap must still make progress, else it blocks itself (and everything
    // behind it) forever.
    const bool pipeline_empty = stats.io_in_flight == 0 && stats.backlog_bytes == limits.deferred_backlog_bytes;
    if (!pipeline_empty && stats.backlog_bytes + est_cpu > limits.decoded_backlog_cap)
    {
      stats.io_denied_backlog++;
      break;
    }
    if (stats.gpu_memory_used + virtual_gpu_used + stats.projected_gpu_bytes + est_gpu > limits.gpu_memory_budget)
    {
      stats.io_denied_gpu++;
      break;
    }
    native_load_request_t req;
    std::memcpy(req.format, node.walker_data.format, sizeof(req.format));
    std::memcpy(req.locations, node.walker_data.locations, sizeof(req.locations));
    req.tree_config = tree_config;
    // Only leaves (with promotion on) can become virtual subnodes, so only they need the salvage blobs shipped
    // back by the wasm worker loader; interior nodes skip the extra transfer.
    req.want_salvage = promote_leaves && node.walker_data.is_leaf;
    node.load_handle = node_loader->request_load(&req, sizeof(req));
    node.io_state = render_node_io_state::loading;
    stats.io_in_flight++;
    stats.io_scheduled++;
    stats.backlog_bytes += est_cpu;
    stats.projected_gpu_bytes += est_gpu;
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
    if (bytes_uploaded >= limits.max_upload_bytes)
      break;
    auto &node = *render_list[entry.index];
    // Unified GPU budget (R7): leave room for the virtual nodes' total (last frame) so real + virtual together
    // are bounded by gpu_memory_budget, not just the real monoliths.
    if (stats.gpu_memory_used + virtual_gpu_used + node.gpu_memory_size > limits.gpu_memory_budget)
      break;

    auto &loaded = node.loaded_data;

    // Normalize attribute if needed (CPU work)
    auto tn0 = clock::now();
    std::shared_ptr<uint8_t[]> normalized_data;
    uint32_t normalized_size = 0;
    bool should_normalize = (attr_min < attr_max) &&
                           !(loaded.attribute_type == dew_type_u16 && loaded.attribute_components == dew_components_3);
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
    callbacks.do_create_buffer(node.gpu_buffers[0], dew_buffer_type_vertex);
    callbacks.do_initialize_buffer(node.gpu_buffers[0], loaded.vertex_type, loaded.vertex_components, int(loaded.vertex_data_size), loaded.vertex_data);

    callbacks.do_create_buffer(node.gpu_buffers[1], dew_buffer_type_vertex);
    if (should_normalize)
      callbacks.do_initialize_buffer(node.gpu_buffers[1], dew_type_r32, loaded.attribute_components, int(normalized_size), normalized_data.get());
    else
      callbacks.do_initialize_buffer(node.gpu_buffers[1], loaded.attribute_type, loaded.attribute_components, int(loaded.attribute_data_size), loaded.attribute_data);

    // Per-point rep_level (u8x1, normalized to [0,1] in the shader) for the per-point LOD test.
    if (loaded.rep_level_data && loaded.rep_level_data_size)
    {
      callbacks.do_create_buffer(node.gpu_buffers[3], dew_buffer_type_vertex);
      callbacks.do_initialize_buffer(node.gpu_buffers[3], dew_type_u8, dew_components_1, int(loaded.rep_level_data_size), loaded.rep_level_data);
    }

    auto offset = to_glm(tree_config.offset) + to_glm(node.offset);
    node.camera_view = glm::mat4(camera_frame.projection * glm::translate(camera_frame.view, offset));
    callbacks.do_create_buffer(node.gpu_buffers[2], dew_buffer_type_uniform);
    callbacks.do_initialize_buffer(node.gpu_buffers[2], dew_type_r32, dew_components_4x4, sizeof(node.camera_view), &node.camera_view);

    bool is_mono = (loaded.draw_type == dew_dyn_points_1);
    node.params_data = glm::vec4(0.0f, 1.0f, 0.0f, is_mono ? 1.0f : 0.0f);
    callbacks.do_create_buffer(node.params_buffer, dew_buffer_type_uniform);
    callbacks.do_initialize_buffer(node.params_buffer, dew_type_r32, dew_components_4, sizeof(node.params_data), &node.params_data);

    node.gpu_state = render_node_gpu_state::uploaded;
    // A spanning leaf may later want virtual subdivision, which needs the pre-reorder morton codes. Salvage the
    // data_handler (already in memory, about to be freed) before release; promotion decides per-frame.
    if (promote_leaves && node.walker_data.is_leaf && !node.resident_handler && node.loaded_data._impl_data)
    {
      auto impl = std::static_pointer_cast<loaded_node_impl_data_t>(node.loaded_data._impl_data);
      node.resident_handler = impl->data_handler;
      // Worker-loader path: the load was REQUESTED while promotion was off (want_salvage false), so no salvage
      // blobs came back and the lift is empty even though promotion is on now. Arm the R5-recovery reload so
      // the promoter re-acquires the handler; otherwise this leaf is stranded on its full-res monolith. (The
      // native loader always carries the handler in _impl_data, so this never arms there.)
      if (!node.resident_handler)
        node.salvage_lost = true;
    }
    // The decoded CPU buffers (several MB of shared_ptr[]) were copied into GL above and are dead now. Freeing
    // them here would cascade ~loaded_node_impl_data_t / ~read_request_t on the render thread (the largest
    // non-GL cost in the frame profile). Hand them to a reap sink drained on a worker instead; releasing the
    // moved-from node clears its now-stale raw pointers.
    if (reap_sink)
      reap_sink->push_back(std::move(node.loaded_data));
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

// Per-point LOD SUBMIT BOUND. The visible density is decided per point in the shader (each point culled by its
// own distance); this only bounds how many points we submit so the shader has everything it might draw. The
// finest level any part of the node needs is set by the NEAREST point (node.cached_distance). Shared core in
// lod_draw_size_from_prefix (point_buffer_render_helper.hpp), reused by the virtual-node emit.
static uint32_t compute_lod_draw_size(const render_node_t &node, const render::frame_camera_cpp_t &camera, const tree_config_t &tree_config, int viewport_height, double render_density_px)
{
  if (!node.has_lod_order || node.point_count == 0)
    return node.point_count;
  return lod_draw_size_from_prefix(node.prefix_count, node.point_count, node.cached_distance, camera.projection[1][1], viewport_height, tree_config.scale, render_density_px);
}

int emit_draws(
    render_list_t &render_list,
    render::callback_manager_t &callbacks,
    const render::frame_camera_cpp_t &camera,
    const tree_config_t &tree_config,
    dew_to_render_t *to_render,
    float fade_duration_ms,
    int viewport_height,
    double render_density_px,
    uint64_t &points_rendered)
{
  int nodes_drawn = 0;
  points_rendered = 0;

  // Per-frame per-point-LOD constants (same for every node). The shader turns view depth gl_Position.w into a
  // grid level: cells = lod_density_scale * w / lod_px_scale; W = log2(cells); keep point iff rep_level >= W.
  const float lod_px_scale = float(camera.projection[1][1] * 0.5 * double(viewport_height));
  const float lod_density_scale = tree_config.scale > 0.0 ? float(render_density_px / tree_config.scale) : 0.0f;

  // Pass 1: steady opaque nodes
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.gpu_state != render_node_gpu_state::uploaded)
      continue;
    if (node.draw_suppressed) // a live virtual cut replaces this leaf's own monolith draw
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

    node.draw_list[0] = {dew_dyn_points_bm_vertex, node.gpu_buffers[0].user_ptr};
    node.draw_list[1] = {dew_dyn_points_bm_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[2] = {dew_dyn_points_bm_camera, node.gpu_buffers[2].user_ptr};
    node.draw_list[3] = {dew_dyn_points_bm_replevel, node.gpu_buffers[3].user_ptr};

    // Submit the prefix down to the finest level the nearest part of the node needs; the shader culls the rest
    // per point (near dense, far coarse). One opaque draw -- no per-node screen-door split.
    const uint32_t draw_size = compute_lod_draw_size(node, camera, tree_config, viewport_height, render_density_px);
    dew_draw_group_t draw_group = {node.draw_type, node.draw_list, 4, int(draw_size), node.walker_data.lod, lod_px_scale, lod_density_scale};
    dew_to_render_add_render_group(to_render, draw_group);

    points_rendered += draw_size;
    nodes_drawn++;
  }

  // Pass 2: fading nodes
  for (auto &np : render_list)
  {
    auto &node = *np;
    if (node.gpu_state != render_node_gpu_state::uploaded)
      continue;
    if (node.draw_suppressed) // a live virtual cut replaces this leaf's own monolith draw
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

    bool is_mono = (node.draw_type == dew_dyn_points_1);
    node.params_data = glm::vec4(alpha, 1.0f, is_mono ? 1.0f : 0.0f, is_mono ? 1.0f : 0.0f);

    if (!node.params_buffer.user_ptr)
    {
      callbacks.do_create_buffer(node.params_buffer, dew_buffer_type_uniform);
      callbacks.do_initialize_buffer(node.params_buffer, dew_type_r32, dew_components_4, sizeof(node.params_data), &node.params_data);
    }
    else
    {
      callbacks.do_modify_buffer(node.params_buffer, 0, sizeof(node.params_data), &node.params_data);
    }

    node.draw_list[0] = {dew_dyn_points_bm_vertex, node.gpu_buffers[0].user_ptr};
    node.draw_list[1] = {dew_dyn_points_bm_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[2] = {dew_dyn_points_bm_camera, node.gpu_buffers[2].user_ptr};
    node.draw_list[3] = {dew_dyn_points_bm_old_color, node.gpu_buffers[1].user_ptr};
    node.draw_list[4] = {dew_dyn_points_bm_params, node.params_buffer.user_ptr};
    node.draw_list[5] = {dew_dyn_points_bm_replevel, node.gpu_buffers[3].user_ptr};

    // Node-level crossfade animates the whole node via its params alpha; same per-point LOD submit + shader
    // cull as the steady pass so density is consistent across the fade<->steady transition.
    const uint32_t draw_size = compute_lod_draw_size(node, camera, tree_config, viewport_height, render_density_px);
    dew_draw_group_t draw_group = {dew_dyn_points_crossfade, node.draw_list, 6, int(draw_size), node.walker_data.lod, lod_px_scale, lod_density_scale};
    dew_to_render_add_render_group(to_render, draw_group);

    points_rendered += draw_size;
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
    // A worker thread may still be in convert_node_data (writing loaded_data + prefix_count/has_lod_order);
    // wait for it before we release/reload, same guard as destroy_render_node.
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
      if (node.gpu_buffers[1].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[1]);
      if (node.gpu_buffers[0].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[0]);
      if (node.gpu_buffers[2].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[2]);
      if (node.gpu_buffers[3].user_ptr)
        callbacks.do_destroy_buffer(node.gpu_buffers[3]);
      if (node.params_buffer.user_ptr)
        callbacks.do_destroy_buffer(node.params_buffer);
      node.gpu_state = render_node_gpu_state::none;
    }
    node.io_state = render_node_io_state::none;
  }
}

} // namespace dew::converter
