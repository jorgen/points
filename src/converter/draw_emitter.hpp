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

namespace points::converter
{

struct draw_result_t
{
  uint64_t points_rendered = 0;
  bool any_animating = false;
};

class draw_emitter_t
{
public:
  draw_result_t emit(std::vector<std::unique_ptr<gpu_node_buffer_t>> &render_buffers,
                     const frame_node_registry_t &registry,
                     const selection_result_t &selection,
                     render::callback_manager_t &callbacks,
                     const render::frame_camera_cpp_t &camera,
                     const tree_config_t &tree_config,
                     render::to_render_t *to_render);
};

} // namespace points::converter
