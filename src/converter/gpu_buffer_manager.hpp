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
#include "data_source.hpp"
#include "frame_node_registry.hpp"
#include "gpu_node_buffer.hpp"
#include "node_selector.hpp"
#include "renderer_callbacks.hpp"

#include <string>

namespace points::converter
{

class gpu_buffer_manager_t
{
public:
  void reconcile(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                 const std::vector<tree_walker_data_t> &walker_subsets,
                 render::callback_manager_t &callbacks,
                 std::unique_ptr<render::node_data_loader_t> &node_loader,
                 size_t &gpu_memory_used);

  int upload_ready(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                   render::callback_manager_t &callbacks,
                   std::unique_ptr<render::node_data_loader_t> &node_loader,
                   size_t &gpu_memory_used,
                   size_t upload_limit,
                   int max_uploads,
                   const render::frame_camera_cpp_t &camera,
                   const std::string &current_attribute_name,
                   double attr_min, double attr_max);

  void schedule_io(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                   const frame_node_registry_t &registry,
                   const selection_result_t &selection,
                   const tree_config_t &tree_config,
                   std::unique_ptr<render::node_data_loader_t> &node_loader,
                   int max_requests);

  void evict(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
             const frame_node_registry_t &registry,
             const selection_result_t &selection,
             const glm::dvec3 &camera_position,
             size_t &gpu_memory_used,
             size_t target_memory,
             render::callback_manager_t &callbacks,
             std::unique_ptr<render::node_data_loader_t> &node_loader);

  void handle_attribute_change(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                               render::callback_manager_t &callbacks,
                               std::unique_ptr<render::node_data_loader_t> &node_loader,
                               size_t &gpu_memory_used);
};

} // namespace points::converter
