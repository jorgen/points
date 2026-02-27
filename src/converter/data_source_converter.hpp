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

#include "compressor.hpp"
#include "converter.hpp"
#include "data_source_node_bbox.hpp"
#include "draw_emitter.hpp"
#include "frame_node_registry.hpp"
#include "gpu_buffer_manager.hpp"
#include "gpu_node_buffer.hpp"
#include "node_selector.hpp"
#include "renderer_callbacks.hpp"
#include <points/render/data_source.h>

#include <memory>
#include <string>
#include <vector>

namespace points::converter
{

struct converter_data_source_t
{
  converter_data_source_t(const std::string &url, render::callback_manager_t &callback_manager);

  void add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render);

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
  double screen_fraction_threshold = 0.5;
  size_t gpu_memory_budget = 512 * 1024 * 1024;
  size_t gpu_memory_used = 0;
  uint64_t point_budget = 10'000'000;

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

  // Cached walker attribute names (rebuilt only on attribute change)
  std::vector<std::string> cached_walker_attribute_names;
  std::string cached_walker_attribute_source;

  // Refactored components
  frame_node_registry_t node_registry;
  node_selector_t node_selector;
  gpu_buffer_manager_t buffer_manager;
  draw_emitter_t draw_emitter;

  std::unique_ptr<node_bbox_data_source_t> bbox_data_source;
  bool show_bounding_boxes = false;
};
} // namespace points::converter
