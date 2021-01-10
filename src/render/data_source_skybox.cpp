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

#include "renderer_p.h"
#include "renderer_callbacks_p.h"
#include "image_decoder_p.h"

namespace points
{
namespace render
{

static void initialize_buffer_from_texture(skybox_texture_t &texture, enum buffer_texture_type_t mapping, buffer_t &buffer)
{
  buffer.texture_type = mapping;
  buffer.width = texture.width;
  buffer.height = texture.height;
  buffer.type = buffer_type_texture;
  buffer.data = texture.image.get();
  buffer.format = buffer_format_u8;
  buffer.components = buffer_components_t(texture.components);
}

skybox_data_source_t::skybox_data_source_t(callback_manager_t &c, skybox_data_t skybox_data)
  : callbacks(c)
{
  inverse_vp_buffer.type = buffer_type_uniform;
  inverse_vp_buffer.format = buffer_format_r32;
  inverse_vp_buffer.components = buffer_components_4x4;
  inverse_vp_buffer.normalize = buffer_normalize_do_not_normalize;
  inverse_vp_buffer.buffer_mapping = int(skybox_inverse_view_projection);
  inverse_vp_buffer.data = &inverse_vp;
  inverse_vp_buffer.data_size = sizeof(inverse_vp);
  callbacks.do_create_buffer(&inverse_vp_buffer);
  callbacks.do_initialize_buffer(&inverse_vp_buffer);

  camera_pos_buffer.type = buffer_type_uniform;
  camera_pos_buffer.format = buffer_format_r32;
  camera_pos_buffer.components = buffer_components_3;
  camera_pos_buffer.normalize = buffer_normalize_do_not_normalize;
  camera_pos_buffer.buffer_mapping = int(skybox_camera_pos);
  camera_pos_buffer.data = &camera_pos;
  camera_pos_buffer.data_size = sizeof(camera_pos);
  callbacks.do_create_buffer(&camera_pos_buffer);
  callbacks.do_initialize_buffer(&camera_pos_buffer);

  cube_texture.buffer_mapping = buffer_texture_cubemap;
  cube_texture.type = buffer_type_texture;
  callbacks.do_create_buffer(&cube_texture);

  std::tuple<void *, int, enum buffer_texture_type_t> data_to_mapping[] = {
    {skybox_data.positive_x, skybox_data.positive_x_size, buffer_texture_cubemap_positive_x}, {skybox_data.negative_x, skybox_data.negative_x_size, buffer_texture_cubemap_negative_x},
    {skybox_data.positive_y, skybox_data.positive_y_size, buffer_texture_cubemap_positive_y}, {skybox_data.negative_y, skybox_data.negative_y_size, buffer_texture_cubemap_negative_y},
    {skybox_data.positive_z, skybox_data.positive_z_size, buffer_texture_cubemap_positive_z}, {skybox_data.negative_z, skybox_data.negative_z_size, buffer_texture_cubemap_negative_z}};

  for (int i = 0; i < 6; i++)
  {
    textures[i].image =
      load_image(std::get<0>(data_to_mapping[i]), std::get<1>(data_to_mapping[i]), textures[i].width, textures[i].height, textures[i].components);
    initialize_buffer_from_texture(textures[i], std::get<2>(data_to_mapping[i]), cube_texture);
    callbacks.do_initialize_buffer(&cube_texture);
  }
  cube_texture.buffer_mapping = skybox_texture_cube;

  vertex_buffer.buffer_mapping = skybox_vertex;
  vertex_buffer.components = buffer_components_2;
  vertex_buffer.format = buffer_format_r32;
  vertex_buffer.normalize = buffer_normalize_do_not_normalize;
  vertex_buffer.type = buffer_type_vertex;
  callbacks.do_create_buffer(&vertex_buffer);

  vertices.reserve(3);
  vertices.emplace_back(-1.0f, -1.0f);
  vertices.emplace_back(3.0f, -1.0f);
  vertices.emplace_back(-1.0f, 3.0f);
  vertex_buffer.data = vertices.data();
  vertex_buffer.data_size = int(sizeof(*vertices.data()) * vertices.size());
  vertex_buffer.data_offset = 0;
  callbacks.do_initialize_buffer(&vertex_buffer);
}

void skybox_data_source_t::add_to_frame(const frame_camera_t &camera, std::vector<draw_group_t> &to_render)
{
  inverse_vp = camera.inverse_view_projection;
  callbacks.do_modify_buffer(&inverse_vp_buffer);

  camera_pos = camera.inverse_view[3];
  callbacks.do_modify_buffer(&camera_pos_buffer);
  draw_buffers[0].data = &inverse_vp_buffer;
  draw_buffers[0].user_ptr = inverse_vp_buffer.user_ptr;
  draw_buffers[1].data = &cube_texture;
  draw_buffers[1].user_ptr = &cube_texture.user_ptr;
  draw_buffers[2].data = &vertex_buffer;
  draw_buffers[2].user_ptr = vertex_buffer.user_ptr;
  draw_buffers[3].data = &camera_pos_buffer;
  draw_buffers[3].user_ptr = camera_pos_buffer.user_ptr;
  to_render.emplace_back();
  auto &draw_group = to_render.back();
  draw_group.buffers = draw_buffers;
  draw_group.buffers_size = sizeof(draw_buffers) / sizeof(*draw_buffers);
  draw_group.draw_type = skybox_triangle;
  draw_group.draw_size = 3;
}

struct skybox_data_source_t *skybox_data_source_create(struct renderer_t *renderer, skybox_data_t data)
{
  return new skybox_data_source_t(renderer->callbacks, data);
}
void skybox_data_source_destroy(struct skybox_data_source_t *skybox_data_source)
{
  delete skybox_data_source;
}
struct data_source_t *skybox_data_source_get(struct skybox_data_source_t *skybox_data_source)
{
  return skybox_data_source;
}

}
}
