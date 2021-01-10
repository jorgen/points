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
#include <aabb_p.h>
#include <points/render/camera.h>
#include <points/render/renderer.h>
#include "data_source_p.h"
#include "glm_include.h"
#include "buffer_p.h"
#include "renderer_callbacks_p.h"
#include <vector>
#include <memory>

namespace points
{
namespace render
{

struct aabb_buffer_t
{
  aabb_t aabb;
  std::vector<glm::vec3> vertices;
  std::unique_ptr<buffer_t> vertices_buffer;

  draw_buffer_t render_list[4];
};


struct aabb_data_source_t : public data_source_t
{
  aabb_data_source_t(callback_manager_t &callback_manager);

  void add_to_frame(const frame_camera_t &camera, std::vector<draw_group_t> &to_render) override;

  callback_manager_t &callbacks;

  std::vector<aabb_buffer_t> aabbs;

  std::vector<uint32_t> aabbs_ids;

  buffer_t camera_buffer;
  glm::mat4 project_view;

  buffer_t index_buffer;
  std::vector<uint16_t> indecies;

  buffer_t color_buffer;
  std::vector<glm::u8vec3> colors;

};
} // namespace render
} // namespace points
