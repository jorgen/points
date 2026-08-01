/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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
#include <dew/render/aabb.h>
#include <dew/render/renderer.h>

#include <memory>
#include <vector>

namespace dew::render
{
struct aabb_buffer_t
{
  dew_aabb_t aabb;
  std::vector<glm::vec3> vertices;
  dew_buffer_t vertices_buffer;

  dew_draw_buffer_t render_list[4];
};
} // namespace dew::render

struct dew_aabb_data_source_t : public dew::render::data_source_cpp_t
{
  dew_aabb_data_source_t(dew::render::callback_manager_t &a_callbacks, const glm::dvec3 &a_offset);

  void add_to_frame(const dew::render::frame_camera_cpp_t &camera, dew_to_render_t *to_render) override;

  dew::render::callback_manager_t &callbacks;

  std::vector<std::unique_ptr<dew::render::aabb_buffer_t>> aabbs;

  std::vector<uint32_t> aabbs_ids;

  dew_buffer_t project_view_buffer;
  glm::mat4 project_view;

  dew_buffer_t index_buffer;
  std::vector<uint16_t> indecies;

  dew_buffer_t color_buffer;
  std::vector<glm::u8vec3> colors;
};
