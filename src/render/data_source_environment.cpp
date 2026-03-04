/************************************************************************
** Points - point cloud management software.
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
#include "data_source_environment.hpp"
#include <points/render/environment_data_source.h>

#include "renderer.hpp"

namespace points::render
{

environment_data_source_t::environment_data_source_t(callback_manager_t &a_callbacks, double ground_z, double grid_size)
  : callbacks(a_callbacks)
  , ground_z_d(ground_z)
  , grid_size_d(grid_size)
  , inverse_vp(1)
  , camera_pos(0)
  , params(float(grid_size), 0.0f, 0.0f, 0.0f)
{
  callbacks.do_create_buffer(inverse_vp_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(inverse_vp_buffer, type_r32, components_4x4, int(sizeof(inverse_vp)), &inverse_vp);

  callbacks.do_create_buffer(camera_pos_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_pos_buffer, type_r32, components_3, int(sizeof(camera_pos)), &camera_pos);

  callbacks.do_create_buffer(params_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(params_buffer, type_r32, components_4, int(sizeof(params)), &params);

  vertices.reserve(3);
  vertices.emplace_back(-1.0f, -1.0f);
  vertices.emplace_back(3.0f, -1.0f);
  vertices.emplace_back(-1.0f, 3.0f);

  callbacks.do_create_buffer(vertex_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_2, int(sizeof(*vertices.data()) * vertices.size()), vertices.data());
}

void environment_data_source_t::add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render)
{
  // Camera-relative inverse VP: removes translation so shader never sees large world coords.
  // direction = (inv_vp_rel * clip_pos).xyz / w  — gives ray direction directly.
  glm::dmat4 view_no_trans = camera.view;
  view_no_trans[3] = glm::dvec4(0, 0, 0, 1);
  inverse_vp = glm::mat4(glm::inverse(camera.projection * view_no_trans));
  callbacks.do_modify_buffer(inverse_vp_buffer, 0, int(sizeof(inverse_vp)), &inverse_vp);

  // Camera pos: xy = position mod grid_size (small, precise in float32), z = height above ground
  glm::dvec3 eye = glm::dvec3(camera.inverse_view[3]);
  camera_pos = glm::vec3(float(fmod(eye.x, grid_size_d)), float(fmod(eye.y, grid_size_d)), float(eye.z - ground_z_d));
  callbacks.do_modify_buffer(camera_pos_buffer, 0, sizeof(camera_pos), &camera_pos);

  callbacks.do_modify_buffer(params_buffer, 0, sizeof(params), &params);

  draw_buffers[0].buffer_mapping = environment_bm_inverse_view_projection;
  draw_buffers[0].user_ptr = inverse_vp_buffer.user_ptr;
  draw_buffers[1].buffer_mapping = environment_bm_camera_pos;
  draw_buffers[1].user_ptr = camera_pos_buffer.user_ptr;
  draw_buffers[2].buffer_mapping = environment_bm_vertex;
  draw_buffers[2].user_ptr = vertex_buffer.user_ptr;
  draw_buffers[3].buffer_mapping = environment_bm_params;
  draw_buffers[3].user_ptr = params_buffer.user_ptr;

  draw_group_t draw_group = {};
  draw_group.buffers = draw_buffers;
  draw_group.buffers_size = 4;
  draw_group.draw_type = environment_bg;
  draw_group.draw_size = 3;
  to_render_add_render_group(to_render, draw_group);
}

struct environment_data_source_t *environment_data_source_create(struct renderer_t *renderer, double ground_z, double grid_size)
{
  return new environment_data_source_t(renderer->callbacks, ground_z, grid_size);
}

void environment_data_source_destroy(struct environment_data_source_t *env)
{
  delete env;
}

struct data_source_t environment_data_source_get(struct environment_data_source_t *env)
{
  return env->data_source;
}

void environment_data_source_set_ground_z(struct environment_data_source_t *env, double ground_z)
{
  env->ground_z_d = ground_z;
}

} // namespace points::render
