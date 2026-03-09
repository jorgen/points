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
#include <cstdint>

namespace points::converter
{

struct frame_timings_t
{
  double tree_walk_ms = 0;
  double buffer_reconciliation_ms = 0;
  double gpu_upload_ms = 0;
  double refine_strategy_ms = 0;
  double frontier_scheduling_ms = 0;
  double draw_emission_ms = 0;
  double eviction_ms = 0;
  double total_ms = 0;
  int registry_node_count = 0;
  int active_set_size = 0;
  int nodes_drawn = 0;
  int transitioning_count = 0;
  int nodes_evicted = 0;
  int nodes_reconcile_destroyed = 0;
  int walker_node_count = 0;
  uint64_t walker_total_points = 0;
  int walker_trees_to_load = 0;
};

struct tree_walker_with_buffer_t
{
  tree_walker_with_buffer_t() = default;
  explicit tree_walker_with_buffer_t(tree_walker_nodes_t &&a_node_data)
    : node_data(std::move(a_node_data))
  {
  }
  tree_walker_with_buffer_t(tree_walker_with_buffer_t &&) = default;
  ~tree_walker_with_buffer_t() = default;
  tree_walker_nodes_t node_data;
};

struct gpu_node_buffer_t
{
  tree_walker_data_t node_info;
  points_draw_type_t draw_type = points_dyn_points_1;
  points_draw_buffer_t render_list[6] = {};
  points_buffer_t render_buffers[3] = {};
  uint32_t point_count = 0;
  std::array<double, 3> offset = {};
  glm::mat4 camera_view = {};
  render::load_handle_t load_handle = render::invalid_load_handle;
  size_t gpu_memory_size = 0;
  size_t attribute_data_size = 0;
  size_t old_color_memory = 0;
  bool rendered = false;

  // Fade-in
  static constexpr int FADE_FRAMES = 10;
  int fade_frame = FADE_FRAMES;

  // Crossfade
  static constexpr int CROSSFADE_FRAMES = 10;
  points_buffer_t old_color_buffer = {};
  bool old_color_valid = false;
  bool old_color_is_mono = false;
  bool awaiting_new_color = false;
  int crossfade_frame = 0;

  // Params uniform
  points_buffer_t params_buffer = {};
  glm::vec4 params_data = {1.0f, 1.0f, 0.0f, 0.0f};
};

} // namespace points::converter
