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

#include <points/render/camera.h>
#include <points/render/renderer.h>
#include "data_source_p.h"
#include "buffer_p.h"
#include "renderer_callbacks_p.h"

#include "glm_include.h"
#include <vector>
#include <memory>
namespace points
{
namespace render
{
struct skybox_resource_t
{
  void *positive_x;
  void *negative_x;
  void *positive_y;
  void *negative_y;
  void *positive_z;
  void *negative_z;
  int size;
  int pitch;
};

struct skybox_data_source_t : public data_source_t
{
  skybox_data_source_t(callback_manager_t &callbacks, skybox_resource_t skybox_resource);

  void add_to_frame(const camera_t &camera, std::vector<draw_group_t> &to_render) override;

  buffer_t vertex_buffer;
  std::vector<glm::vec3> vertices;

  buffer_t cube_texture;
};
} // namespace render
} // namespace points
