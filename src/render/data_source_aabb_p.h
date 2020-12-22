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
#include "buffer_data_p.h"

#include <vector>
#include <memory>
namespace points
{
namespace render
{

struct aabb_buffer
{
  aabb aabb;
  std::vector<glm::vec3> vertices;
  std::unique_ptr<buffer_data> vertices_buffer_data;

  buffer render_list[3];
};


struct aabb_data_source : public data_source
{
  aabb_data_source();

  void add_to_frame(const renderer &renderer, const camera &camera, std::vector<buffer> &to_add, std::vector<buffer> &to_update, std::vector<buffer> &to_remove, std::vector<draw_group> &to_render) override;

  std::vector<aabb_buffer> aabbs;
  std::vector<std::pair<buffer, buffer_data>> to_remove_buffers;

  std::vector<uint32_t> aabbs_ids;

  buffer index_buffer;
  std::unique_ptr<buffer_data> index_buffer_data;
  std::vector<uint16_t> indecies;

  buffer color_buffer;
  std::unique_ptr<buffer_data> color_buffer_data;
  std::vector<glm::u8vec3> colors;

};
} // namespace render
} // namespace points
