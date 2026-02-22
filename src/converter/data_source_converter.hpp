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
#pragma once

#include "buffer.hpp"
#include "compressor.hpp"
#include "converter.hpp"
#include "frustum_tree_walker.hpp"
#include "node_data_loader.hpp"
#include "renderer_callbacks.hpp"
#include <points/render/data_source.h>

#include <memory>
#include <string>
#include <vector>

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
};

struct tree_walker_with_buffer_t
{
  tree_walker_with_buffer_t() = default;
  explicit tree_walker_with_buffer_t(tree_walker_nodes_t &&node_data)
    : node_data(std::move(node_data))
  {
  }
  tree_walker_with_buffer_t(tree_walker_with_buffer_t &&) = default;
  ~tree_walker_with_buffer_t() = default;
  tree_walker_nodes_t node_data;
};

struct gpu_node_buffer_t
{
  tree_walker_data_t node_info;
  render::draw_type_t draw_type = render::dyn_points_1;
  render::draw_buffer_t render_list[4] = {};
  render::buffer_t render_buffers[3] = {};
  uint32_t point_count = 0;
  std::array<double, 3> offset = {};
  glm::mat4 camera_view = {};
  render::load_handle_t load_handle = render::invalid_load_handle;
  size_t gpu_memory_size = 0;
  bool rendered = false;
};

struct converter_data_source_t
{
  converter_data_source_t(const std::string &url, render::callback_manager_t &callback_manager);

  void add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render);
  void destroy_gpu_buffer(gpu_node_buffer_t &buf);

  const std::string url;
  error_t error;
  processor_t processor;
  render::callback_manager_t &callbacks;
  render::data_source_t data_source;

  std::mutex mutex;
  std::string current_attribute_name;
  std::string next_attribute_name;

  int viewport_width = 1920;
  int viewport_height = 1080;
  double pixel_error_threshold = 2.0;
  bool auto_adjust_threshold = true;
  size_t gpu_memory_budget = 512 * 1024 * 1024;
  size_t gpu_memory_used = 0;
  double effective_pixel_error_threshold = 2.0;

  render::buffer_t index_buffer;
  std::vector<tree_walker_with_buffer_t> current_tree_nodes[2];
  bool current_tree_nodes_index = false;

  std::unique_ptr<render::node_data_loader_t> node_loader;
  std::vector<std::unique_ptr<gpu_node_buffer_t>> render_buffers;

  uint64_t points_rendered_last_frame = 0;
  frame_timings_t frame_timings;

  compression_stats_t attribute_stats;
  double current_attr_min = 0.0;
  double current_attr_max = 1.0;
};
} // namespace points::converter
