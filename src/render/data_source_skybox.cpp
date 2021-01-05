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
#include "data_source_skybox_p.h"

namespace points
{
namespace render
{

skybox_data_source_t::skybox_data_source_t(callback_manager_t &callbacks, skybox_data_t skybox_data)
{
  (void)skybox_data;
  vertex_buffer.buffer_mapping = skybox_vertex;
  vertex_buffer.components = component_2;
  vertex_buffer.format = buffer_format_r32;
  vertex_buffer.normalize = buffer_normalize_do_not_normalize;
  vertex_buffer.type = buffer_type_vertex;
  callbacks.do_create_buffer(&vertex_buffer);

  vertices.reserve(3);
  vertices.emplace_back(-1.0f, -1.0f, 0.0f);
  vertices.emplace_back(3.0f, -1.0f, 0.0f);
  vertices.emplace_back(-1.0f, 3.0f, 0.0f);
  vertex_buffer.data = vertices.data();
  vertex_buffer.data_size = 0;
  vertex_buffer.data_offset = 0;
  callbacks.do_initialize_buffer(&vertex_buffer);



}

void skybox_data_source_t::add_to_frame(const camera_t &camera, std::vector<draw_group_t> &to_render)
{
  (void)camera;
  (void)to_render;

}

}
}
