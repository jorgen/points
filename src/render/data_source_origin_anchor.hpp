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
#include "data_source.hpp"
#include "glm_include.hpp"
#include "renderer_callbacks.hpp"
#include <points/render/renderer.h>

namespace points::render
{

struct origin_anchor_data_source_t : public data_source_cpp_t
{
  origin_anchor_data_source_t(callback_manager_t &callbacks, const glm::dvec3 &center, double arrow_size);

  void add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render) override;
  void rebuild_mesh();

  callback_manager_t &callbacks;

  glm::dvec3 center;
  double arrow_size;

  buffer_t camera_buffer;
  glm::mat4 camera_matrix;

  buffer_t vertex_buffer;
  std::vector<glm::vec3> vertices;

  buffer_t color_buffer;
  std::vector<glm::u8vec4> colors;

  buffer_t index_buffer;
  std::vector<uint32_t> indices;

  draw_buffer_t render_list[4];
};

} // namespace points::render
