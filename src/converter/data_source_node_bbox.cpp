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
#include "data_source_node_bbox.hpp"

#include <points/common/format.h>
#include "renderer.hpp"

namespace points::converter
{

static void add_box_edges(std::vector<glm::vec3> &vertices, std::vector<glm::u8vec3> &colors,
                          const node_bbox_t &box, const glm::u8vec3 &color)
{
  // 8 corners of the AABB
  // 0: min.x, min.y, min.z    4: min.x, max.y, min.z
  // 1: max.x, min.y, min.z    5: max.x, max.y, min.z
  // 2: min.x, min.y, max.z    6: min.x, max.y, max.z
  // 3: max.x, min.y, max.z    7: max.x, max.y, max.z
  glm::vec3 c[8];
  c[0] = glm::vec3(box.min);
  c[1] = glm::vec3(box.max.x, box.min.y, box.min.z);
  c[2] = glm::vec3(box.min.x, box.min.y, box.max.z);
  c[3] = glm::vec3(box.max.x, box.min.y, box.max.z);
  c[4] = glm::vec3(box.min.x, box.max.y, box.min.z);
  c[5] = glm::vec3(box.max.x, box.max.y, box.min.z);
  c[6] = glm::vec3(box.min.x, box.max.y, box.max.z);
  c[7] = glm::vec3(box.max);

  // 12 edges = 24 vertices
  auto edge = [&](int a, int b) {
    vertices.push_back(c[a]);
    vertices.push_back(c[b]);
    colors.push_back(color);
    colors.push_back(color);
  };

  // Bottom face (min.y)
  edge(0, 1);
  edge(1, 3);
  edge(3, 2);
  edge(2, 0);

  // Top face (max.y)
  edge(4, 5);
  edge(5, 7);
  edge(7, 6);
  edge(6, 4);

  // Verticals
  edge(0, 4);
  edge(1, 5);
  edge(2, 6);
  edge(3, 7);
}

node_bbox_data_source_t::node_bbox_data_source_t(render::callback_manager_t &callbacks)
  : callbacks(callbacks)
{
  callbacks.do_create_buffer(camera_buffer, render::buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_buffer, type_r32, components_4x4, sizeof(camera_matrix), &camera_matrix);
}

node_bbox_data_source_t::~node_bbox_data_source_t()
{
  if (buffers_created)
  {
    callbacks.do_destroy_buffer(vertex_buffer);
    callbacks.do_destroy_buffer(color_buffer);
  }
  callbacks.do_destroy_buffer(camera_buffer);
}

void node_bbox_data_source_t::update_boxes(const std::vector<node_bbox_t> &loose_boxes,
                                            const std::vector<node_bbox_t> &tight_boxes)
{
  vertices.clear();
  colors.clear();

  glm::u8vec3 loose_color(255, 255, 0);  // yellow
  glm::u8vec3 tight_color(0, 255, 255);  // cyan

  for (auto &box : loose_boxes)
    add_box_edges(vertices, colors, box, loose_color);
  for (auto &box : tight_boxes)
    add_box_edges(vertices, colors, box, tight_color);

  line_count = int(vertices.size()) / 2;

  if (vertices.empty())
    return;

  if (!buffers_created)
  {
    callbacks.do_create_buffer(vertex_buffer, render::buffer_type_vertex);
    callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_3,
                                   int(vertices.size() * sizeof(vertices[0])), vertices.data());
    callbacks.do_create_buffer(color_buffer, render::buffer_type_vertex);
    callbacks.do_initialize_buffer(color_buffer, type_u8, components_3,
                                   int(colors.size() * sizeof(colors[0])), colors.data());
    buffers_created = true;
  }
  else
  {
    callbacks.do_destroy_buffer(vertex_buffer);
    callbacks.do_create_buffer(vertex_buffer, render::buffer_type_vertex);
    callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_3,
                                   int(vertices.size() * sizeof(vertices[0])), vertices.data());
    callbacks.do_destroy_buffer(color_buffer);
    callbacks.do_create_buffer(color_buffer, render::buffer_type_vertex);
    callbacks.do_initialize_buffer(color_buffer, type_u8, components_3,
                                   int(colors.size() * sizeof(colors[0])), colors.data());
  }
}

void node_bbox_data_source_t::add_to_frame(const render::frame_camera_cpp_t &camera, render::to_render_t *to_render)
{
  if (!enabled || line_count <= 0)
    return;

  camera_matrix = glm::mat4(camera.projection * camera.view);
  callbacks.do_modify_buffer(camera_buffer, 0, sizeof(camera_matrix), &camera_matrix);

  render_list[0].buffer_mapping = render::node_bbox_bm_camera;
  render_list[0].user_ptr = camera_buffer.user_ptr;
  render_list[1].buffer_mapping = render::node_bbox_bm_position;
  render_list[1].user_ptr = vertex_buffer.user_ptr;
  render_list[2].buffer_mapping = render::node_bbox_bm_color;
  render_list[2].user_ptr = color_buffer.user_ptr;

  render::draw_group_t draw_group = {};
  draw_group.buffers = render_list;
  draw_group.buffers_size = 3;
  draw_group.draw_type = render::draw_type_t::node_bbox_lines;
  draw_group.draw_size = line_count * 2;
  render::to_render_add_render_group(to_render, draw_group);
}

} // namespace points::converter
