/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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
#include "data_source.hpp"
#include "renderer_callbacks.hpp"
#include <points/render/aabb.h>
#include <points/render/renderer.h>

#include "glm_include.hpp"

#include <string>
#include <vector>

struct points_flat_points_data_source_t : public points::render::data_source_cpp_t
{
  points_flat_points_data_source_t(points::render::callback_manager_t &callbacks, std::string url);

  void add_to_frame(const points::render::frame_camera_cpp_t &camera, points_to_render_t *to_render) override;

  points::render::callback_manager_t &callbacks;

  std::vector<glm::vec3> vertices;
  points_buffer_t vertex_buffer;

  std::vector<glm::u8vec3> colors;
  points_buffer_t color_buffer;

  points_aabb_t aabb;

  points_buffer_t project_view_buffer;
  glm::mat4 project_view;
  double offset[3];

  points_draw_buffer_t render_list[3];
};
