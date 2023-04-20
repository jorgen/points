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
#include <points/render/draw_group.h>
#include "buffer.hpp"
#include "renderer_callbacks.hpp"
#include "converter.hpp"
#include "frustum_tree_walker.hpp"

#include <vector>
#include <memory>
#include <array>

namespace points
{
namespace converter
{

struct dyn_points_draw_buffer_t
{
  render::draw_buffer_t render_list[3];
  render::buffer_t render_buffers[3];
  std::unique_ptr<uint8_t[]> vertex_data;
  buffer_t vertex_data_info;
  points::type_t point_type;
  int point_count;
  std::array<double,3> offset;
  std::array<double,3> scale;
};

struct buffers_t
{
  std::vector<dyn_points_draw_buffer_t> data;
  bool fetching_data = false;
  bool has_data = false;
};

struct tree_walker_with_buffer_t
{
  tree_walker_with_buffer_t() = default;
  tree_walker_with_buffer_t(tree_walker_nodes_t &&node_data)
    : node_data(std::move(node_data))
  {
    for (int i = 0; i < 5; i++)
    {
      buffers[i].resize(this->node_data.point_subsets[i].size());
    }
  }
  tree_walker_with_buffer_t(tree_walker_with_buffer_t &&) = default;
  ~tree_walker_with_buffer_t() = default;
  tree_walker_nodes_t node_data;
  std::vector<buffers_t> buffers[5];
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

  render::buffer_t index_buffer;
  std::vector<tree_walker_with_buffer_t> current_tree_nodes[2];
  bool current_tree_nodes_index = false;

  std::vector<dyn_points_draw_buffer_t> buffers;

};
} // namespace render
} // namespace points
