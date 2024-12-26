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
#include "data_source_skybox.hpp"

#include "renderer.hpp"
#include "renderer_callbacks.hpp"
#include "image_decoder.hpp"

namespace points::render
{

skybox_data_source_t::skybox_data_source_t(callback_manager_t &c, skybox_data_t skybox_data)
  : callbacks(c)
{
  callbacks.do_create_buffer(inverse_vp_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(inverse_vp_buffer, type_r32, components_4x4, int(sizeof(inverse_vp)), &inverse_vp);

  callbacks.do_create_buffer(camera_pos_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_pos_buffer, type_r32, components_3, int(sizeof(camera_pos)), &camera_pos);

  callbacks.do_create_texture(cube_texture, texture_type_cubemap);
  std::tuple<void *, int, enum texture_type_t> data_to_mapping[] = {
    {skybox_data.positive_x, skybox_data.positive_x_size, texture_type_cubemap_positive_x}, {skybox_data.negative_x, skybox_data.negative_x_size, texture_type_cubemap_negative_x},
    {skybox_data.positive_y, skybox_data.positive_y_size, texture_type_cubemap_positive_y}, {skybox_data.negative_y, skybox_data.negative_y_size, texture_type_cubemap_negative_y},
    {skybox_data.positive_z, skybox_data.positive_z_size, texture_type_cubemap_positive_z}, {skybox_data.negative_z, skybox_data.negative_z_size, texture_type_cubemap_negative_z}};
  for (int i = 0; i < 6; i++)
  {
    textures[i].image =
      load_image(std::get<0>(data_to_mapping[i]), std::get<1>(data_to_mapping[i]), textures[i].width, textures[i].height, textures[i].components);
    int size[3] = {textures[i].width, textures[i].height, 0};
    callbacks.do_initialize_texture(cube_texture, std::get<2>(data_to_mapping[i]), type_u8, components_t(textures[i].components), size,
                                    textures[i].image.get());
  }
  callbacks.do_create_buffer(vertex_buffer, buffer_type_vertex);
  vertices.reserve(3);
  vertices.emplace_back(-1.0f, -1.0f);
  vertices.emplace_back(3.0f, -1.0f);
  vertices.emplace_back(-1.0f, 3.0f);
  callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_2,
                                 int(sizeof(*vertices.data()) * vertices.size()), vertices.data());
}

void skybox_data_source_t::add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render)
{
  inverse_vp = camera.inverse_view_projection;
  callbacks.do_modify_buffer(inverse_vp_buffer, 0, int(sizeof(inverse_vp)), &inverse_vp);

  camera_pos = camera.inverse_view[3];
  callbacks.do_modify_buffer(camera_pos_buffer, 0, sizeof(camera_pos), &camera_pos);
  draw_buffers[0].buffer_mapping = skybox_bm_inverse_view_projection;
  draw_buffers[0].user_ptr = inverse_vp_buffer.user_ptr;
  draw_buffers[1].buffer_mapping = skybox_bm_cube_map_texture;
  draw_buffers[1].user_ptr = &cube_texture.user_ptr;
  draw_buffers[2].buffer_mapping = skybox_bm_vertex;
  draw_buffers[2].user_ptr = vertex_buffer.user_ptr;
  draw_buffers[3].buffer_mapping = skybox_bm_camera_pos;
  draw_buffers[3].user_ptr = camera_pos_buffer.user_ptr;
  draw_group_t draw_group;
  draw_group.buffers = draw_buffers;
  draw_group.buffers_size = sizeof(draw_buffers) / sizeof(*draw_buffers);
  draw_group.draw_type = skybox_triangle;
  draw_group.draw_size = 3;
  to_render_add_render_group(to_render, draw_group);
}

struct skybox_data_source_t *skybox_data_source_create(struct renderer_t *renderer, skybox_data_t data)
{
  return new skybox_data_source_t(renderer->callbacks, data);
}
void skybox_data_source_destroy(struct skybox_data_source_t *skybox_data_source)
{
  delete skybox_data_source;
}
struct data_source_t skybox_data_source_get(struct skybox_data_source_t *skybox_data_source)
{
  return skybox_data_source->data_source;
}

}

