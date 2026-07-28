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

#include "buffer.hpp"
#include "frustum_tree_walker.hpp"
#include "node_data_loader.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace points::converter
{

enum class render_node_io_state : uint8_t
{
  none,
  loading,
  converting,
  loaded,
};

enum class render_node_gpu_state : uint8_t
{
  none,
  uploaded,
};

enum class render_node_fade_state : uint8_t
{
  fade_in,
  steady,
  fade_out,
};

struct render_node_t
{
  tree_walker_data_t walker_data;

  render_node_io_state io_state = render_node_io_state::none;
  render::load_handle_t load_handle = render::invalid_load_handle;
  render::loaded_node_data_t loaded_data;
  std::atomic<bool> convert_done{false};

  render_node_gpu_state gpu_state = render_node_gpu_state::none;
  points_draw_type_t draw_type = points_dyn_points_1;
  // [0]=vertex, [1]=color, [2]=camera uniform, [3]=rep_level (per-point LOD, u8x1)
  points_buffer_t gpu_buffers[4] = {};
  points_draw_buffer_t draw_list[6] = {};
  uint32_t point_count = 0;
  std::array<double, 3> offset = {};
  glm::mat4 camera_view = {};
  size_t gpu_memory_size = 0;

  render_node_fade_state fade_state = render_node_fade_state::fade_in;
  float fade_ms = 0.0f;

  points_buffer_t params_buffer = {};
  glm::vec4 params_data = {1.0f, 1.0f, 0.0f, 0.0f};

  double cached_distance = 0.0;

  // Runtime per-node LOD (Approach B): points are uploaded reordered coarse->fine; prefix_count[k] is the
  // draw count for render grid width k-1. Copied from loaded_data at convert time (survives the release()).
  std::array<uint32_t, 64> prefix_count = {};
  bool has_lod_order = false;
};

struct frame_timings_t
{
  double tree_walk_ms = 0;
  double build_render_list_ms = 0;
  double scan_classify_ms = 0;
  double schedule_io_ms = 0;
  double normalize_ms = 0;
  double gpu_upload_ms = 0;
  double fade_ms = 0;
  double emit_ms = 0;
  double total_ms = 0;
  int walker_node_count = 0;
  uint64_t walker_total_points = 0;
  int walker_trees_to_load = 0;
  int render_list_size = 0;
  int nodes_drawn = 0;
  int io_in_flight = 0;
  int nodes_loading = 0;
  int nodes_converting = 0;
  int nodes_uploaded = 0;
  int nodes_fading_in = 0;
  int nodes_fading_out = 0;
  int uploads_this_frame = 0;
};

} // namespace points::converter
