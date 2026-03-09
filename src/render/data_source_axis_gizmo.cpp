/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
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
#include "data_source_axis_gizmo.hpp"
#include <points/render/axis_gizmo_data_source.h>

#include "renderer.hpp"

using namespace points::render;

points_axis_gizmo_data_source_t::points_axis_gizmo_data_source_t(callback_manager_t &a_callbacks, const glm::dvec3 &a_center, double a_axis_length)
  : callbacks(a_callbacks)
  , center(a_center)
  , axis_length(a_axis_length)
  , camera_matrix(1)
{
  callbacks.do_create_buffer(camera_buffer, points_buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_buffer, points_type_r32, points_components_4x4, sizeof(camera_matrix), &camera_matrix);

  // 6 vertices: origin+tip for each axis
  rebuild_vertices();
  callbacks.do_create_buffer(vertex_buffer, points_buffer_type_vertex);
  callbacks.do_initialize_buffer(vertex_buffer, points_type_r32, points_components_3, int(vertices.size() * sizeof(vertices[0])), vertices.data());

  // Colors: R for X, G for Y, B for Z (two vertices per axis, same color)
  colors.resize(6);
  colors[0] = glm::u8vec3(255, 0, 0);
  colors[1] = glm::u8vec3(255, 0, 0);
  colors[2] = glm::u8vec3(0, 255, 0);
  colors[3] = glm::u8vec3(0, 255, 0);
  colors[4] = glm::u8vec3(0, 0, 255);
  colors[5] = glm::u8vec3(0, 0, 255);
  callbacks.do_create_buffer(color_buffer, points_buffer_type_vertex);
  callbacks.do_initialize_buffer(color_buffer, points_type_u8, points_components_3, int(colors.size() * sizeof(colors[0])), colors.data());
}

void points_axis_gizmo_data_source_t::rebuild_vertices()
{
  vertices.resize(6);
  glm::vec3 o(0.0f);
  float len = float(axis_length);
  // X axis
  vertices[0] = o;
  vertices[1] = glm::vec3(len, 0, 0);
  // Y axis
  vertices[2] = o;
  vertices[3] = glm::vec3(0, len, 0);
  // Z axis
  vertices[4] = o;
  vertices[5] = glm::vec3(0, 0, len);
}

void points_axis_gizmo_data_source_t::add_to_frame(const frame_camera_cpp_t &camera, points_to_render_t *to_render)
{
  // Extract rotation only (strip translation/scale from view)
  glm::dmat4 rotation_only = glm::dmat4(glm::dmat3(camera.view));

  // Fixed orthographic projection sized to fit the axis lines
  float margin = float(axis_length) * 1.5f;
  glm::mat4 ortho = glm::ortho(-margin, margin, -margin, margin, -margin, margin);

  camera_matrix = ortho * glm::mat4(rotation_only);
  callbacks.do_modify_buffer(camera_buffer, 0, sizeof(camera_matrix), &camera_matrix);

  render_list[0].buffer_mapping = points_axis_gizmo_bm_position;
  render_list[0].user_ptr = vertex_buffer.user_ptr;
  render_list[1].buffer_mapping = points_axis_gizmo_bm_color;
  render_list[1].user_ptr = color_buffer.user_ptr;
  render_list[2].buffer_mapping = points_axis_gizmo_bm_camera;
  render_list[2].user_ptr = camera_buffer.user_ptr;

  points_draw_group_t draw_group = {};
  draw_group.buffers = render_list;
  draw_group.buffers_size = 3;
  draw_group.draw_type = points_draw_type_t::points_axis_gizmo_lines;
  draw_group.draw_size = 6;
  points_to_render_add_render_group(to_render, draw_group);
}

struct points_axis_gizmo_data_source_t *points_axis_gizmo_data_source_create(struct points_renderer_t *renderer, const double center[3], double axis_length)
{
  return new points_axis_gizmo_data_source_t(renderer->callbacks, glm::dvec3(center[0], center[1], center[2]), axis_length);
}

void points_axis_gizmo_data_source_destroy(struct points_axis_gizmo_data_source_t *gizmo)
{
  delete gizmo;
}

struct points_data_source_t points_axis_gizmo_data_source_get(struct points_axis_gizmo_data_source_t *gizmo)
{
  return gizmo->data_source;
}

void points_axis_gizmo_data_source_set_center(struct points_axis_gizmo_data_source_t *gizmo, const double center[3])
{
  gizmo->center = glm::dvec3(center[0], center[1], center[2]);
}

void points_axis_gizmo_data_source_set_axis_length(struct points_axis_gizmo_data_source_t *gizmo, double axis_length)
{
  gizmo->axis_length = axis_length;
  gizmo->rebuild_vertices();
  gizmo->callbacks.do_modify_buffer(gizmo->vertex_buffer, 0, int(gizmo->vertices.size() * sizeof(gizmo->vertices[0])), gizmo->vertices.data());
}
