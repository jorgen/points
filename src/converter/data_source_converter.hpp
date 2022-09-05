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

#include <points/render/camera.h>
#include <points/render/renderer.h>
#include <points/render/data_source.h>
#include <points/render/aabb_data_source.h>
#include "data_source.hpp"
#include "glm_include.hpp"
#include "buffer.hpp"
#include "renderer_callbacks.hpp"
#include "converter.hpp"
#include "frustum_tree_walker.hpp"

#include <vector>
#include <memory>

namespace points
{
namespace converter
{

struct tree_walker_with_buffer_t
{
  tree_walker_nodes_t node_data;
  std::vector<buffer_t> buffers[5];
};

struct converter_data_source_t
{
  converter_data_source_t(converter_t *converter, render::callback_manager_t &callback_manager);

  void add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render);

  converter_t *converter;
  render::callback_manager_t &callbacks;
  render::data_source_t data_source;

  render::aabb_t aabb;

  std::shared_ptr<frustum_tree_walker_t> back_buffer;

  render::buffer_t project_view_buffer;
  glm::dmat4 project_view;

  render::buffer_t index_buffer;
  std::vector<tree_walker_with_buffer_t> current_tree_nodes;

};
} // namespace render
} // namespace points
