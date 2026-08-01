/************************************************************************
** dewfall - point cloud management software.
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

#include <dew/render/camera.h>
#include <dew/render/renderer.h>
#include <dew/render/skybox_data_source.h>
#include "data_source.hpp"
#include "buffer.hpp"
#include "renderer_callbacks.hpp"

#include "glm_include.hpp"
#include <vector>
#include <memory>

namespace dew::render
{
struct skybox_texture_t
{
  std::unique_ptr<uint8_t, decltype(&free)> image = {nullptr, &free};
  int width = 0;
  int height = 0;
  int components = 0;
};
} // namespace dew::render

struct dew_skybox_data_source_t : public dew::render::data_source_cpp_t
{
  dew_skybox_data_source_t(dew::render::callback_manager_t &callbacks, dew_skybox_data_t skybox_data);

  void add_to_frame(const dew::render::frame_camera_cpp_t &camera, dew_to_render_t *to_render) override;

  dew::render::callback_manager_t &callbacks;

  dew_buffer_t inverse_vp_buffer;
  glm::mat4 inverse_vp;

  dew_buffer_t camera_pos_buffer;
  glm::vec3 camera_pos;

  dew_buffer_t vertex_buffer;
  std::vector<glm::vec2> vertices;

  dew_buffer_t cube_texture;

  dew::render::skybox_texture_t textures[6];

  dew_draw_buffer_t draw_buffers[4];
};
