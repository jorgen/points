/************************************************************************
** dewfall - point cloud management software.
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
#include <dew/render/renderer.h>

struct dew_environment_data_source_t : public dew::render::data_source_cpp_t
{
  dew_environment_data_source_t(dew::render::callback_manager_t &callbacks, double ground_z, double grid_size);

  void add_to_frame(const dew::render::frame_camera_cpp_t &camera, dew_to_render_t *to_render) override;

  dew::render::callback_manager_t &callbacks;

  double ground_z_d;
  double grid_size_d;

  dew_buffer_t inverse_vp_buffer;
  glm::mat4 inverse_vp;

  dew_buffer_t camera_pos_buffer;
  glm::vec3 camera_pos;

  dew_buffer_t params_buffer;
  glm::vec4 params;

  dew_buffer_t vertex_buffer;
  std::vector<glm::vec2> vertices;

  dew_draw_buffer_t draw_buffers[4];
};
