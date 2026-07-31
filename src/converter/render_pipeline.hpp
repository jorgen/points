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

struct io_upload_stats_t
{
  int io_in_flight = 0;
  int io_scheduled = 0;
  int uploads_done = 0;
  size_t gpu_memory_used = 0;
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
    int max_concurrent_io,
    int max_new_io_per_frame,
    size_t max_upload_bytes,
    size_t gpu_memory_budget,
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
