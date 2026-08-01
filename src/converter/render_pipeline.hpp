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
#pragma once

#include "conversion_types.hpp"
#include "render_node.hpp"
#include "renderer_callbacks.hpp"

#include <memory>
#include <vector>

namespace vio { class thread_pool_t; }

struct points_to_render_t;

namespace points::render
{
struct frame_camera_cpp_t;
}

namespace points::converter
{

using render_node_ptr = std::unique_ptr<render_node_t>;
using render_list_t = std::vector<render_node_ptr>;

struct priority_entry_t
{
  int index;
  double distance;
};

static constexpr float default_fade_duration_ms = 300.0f;

bool render_node_less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs);

// True if a departed node still has a worker job in flight (convert / resident-build / virtual materialize).
// destroy_render_node would spin-wait on such a node; the render list defers it instead (see build_render_list).
bool node_is_busy(const render_node_t &node);

render_list_t build_render_list(
    const std::vector<tree_walker_data_t> &walker_nodes,
    render_list_t &&previous_list,
    float fade_duration_ms,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader,
    size_t *virtual_gpu_used,
    render_list_t &deferred_destroy);

// Per-frame IO/upload limits. The count limits throttle scheduling churn; the byte caps are what actually
// bound CPU-heap growth: decoded_backlog_cap refuses new IO once the estimated in-flight + decoded-but-not-
// uploaded bytes reach it (a node that finishes decoding no longer frees a slot for more IO), and the GPU-fit
// pre-check refuses IO for nodes the GPU budget could not accept anyway (no decode-then-stall waste).
struct io_limits_t
{
  int max_concurrent_io = 64;
  int max_new_io_per_frame = 16;
  size_t max_upload_bytes = 6 * 1024 * 1024;
  size_t gpu_memory_budget = 512 * 1024 * 1024;
  size_t decoded_backlog_cap = 256 * 1024 * 1024;
  // CPU bytes still pinned by departed nodes parked in pending_destroy (decoded buffers whose worker job
  // hasn't finished); they share the same heap, so they pre-charge the backlog.
  size_t deferred_backlog_bytes = 0;
};

struct io_upload_stats_t
{
  int io_in_flight = 0;
  int io_scheduled = 0;
  int uploads_done = 0;
  int io_denied_backlog = 0; // IO refused: decoded-backlog byte cap reached
  int io_denied_gpu = 0;     // IO refused: node wouldn't fit the GPU budget
  size_t gpu_memory_used = 0;
  size_t backlog_bytes = 0;       // estimated CPU bytes held by loading/converting/loaded nodes this frame
  size_t projected_gpu_bytes = 0; // GPU bytes the in-flight pipeline will claim once uploaded
  double scan_classify_ms = 0;
  double schedule_io_ms = 0;
  double normalize_ms = 0;
  double gpu_upload_ms = 0;
};

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
    std::vector<render::loaded_node_data_t> *reap_sink);

void update_fades(
    render_list_t &render_list,
    float delta_ms,
    float fade_duration_ms);

int emit_draws(
    render_list_t &render_list,
    render::callback_manager_t &callbacks,
    const render::frame_camera_cpp_t &camera,
    const tree_config_t &tree_config,
    points_to_render_t *to_render,
    float fade_duration_ms,
    int viewport_height,
    double render_density_px,
    uint64_t &points_rendered);

void destroy_render_node(
    render_node_t &node,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader,
    size_t *virtual_gpu_used);

void handle_attribute_change(
    render_list_t &render_list,
    render::callback_manager_t &callbacks,
    render::node_data_loader_t *node_loader);

} // namespace points::converter
