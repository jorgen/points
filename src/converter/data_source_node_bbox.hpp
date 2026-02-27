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

#include "data_source.hpp"
#include "renderer_callbacks.hpp"
#include <glm_include.hpp>
#include <points/render/renderer.h>

#include <vector>

namespace points::converter
{

struct node_bbox_t
{
  glm::dvec3 min;
  glm::dvec3 max;
};

struct node_bbox_data_source_t : public render::data_source_cpp_t
{
  node_bbox_data_source_t(render::callback_manager_t &callbacks);
  ~node_bbox_data_source_t();

  void update_boxes(const std::vector<node_bbox_t> &loose_boxes,
                    const std::vector<node_bbox_t> &tight_boxes);
  void add_to_frame(const render::frame_camera_cpp_t &camera, render::to_render_t *to_render) override;

  render::callback_manager_t &callbacks;
  bool enabled = false;

  render::buffer_t camera_buffer = {};
  glm::mat4 camera_matrix = glm::mat4(1);

  render::buffer_t vertex_buffer = {};
  render::buffer_t color_buffer = {};
  bool buffers_created = false;

  std::vector<glm::vec3> vertices;
  std::vector<glm::u8vec3> colors;
  int line_count = 0;

  render::draw_buffer_t render_list[3] = {};
};

} // namespace points::converter
