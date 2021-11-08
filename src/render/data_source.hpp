/************************************************************************
** Points - point cloud management software.
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

#include <points/render/camera.h>
#include <points/render/renderer.h>
#include "glm_include.hpp"
#include <vector>

namespace points
{
namespace render
{
struct frame_camera_t
{
  glm::dmat4 view;
  glm::dmat4 projection;
  glm::dmat4 view_projection;
  glm::dmat4 inverse_view;
  glm::dmat4 inverse_projection;
  glm::dmat4 inverse_view_projection;
};

struct data_source_t
{
  virtual ~data_source_t();
  virtual void add_to_frame(const frame_camera_t &camera, std::vector<draw_group_t> &to_render) = 0;
};
} // namespace render
} // namespace points

