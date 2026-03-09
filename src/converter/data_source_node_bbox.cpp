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
                          const node_bbox_t &box, const glm::u8vec3 &color,
                          const glm::dvec3 &offset)
{
  // 8 corners of the AABB, shifted by offset so float precision is sufficient
  // 0: min.x, min.y, min.z    4: min.x, max.y, min.z
  // 1: max.x, min.y, min.z    5: max.x, max.y, min.z
  // 2: min.x, min.y, max.z    6: min.x, max.y, max.z
  // 3: max.x, min.y, max.z    7: max.x, max.y, max.z
  glm::vec3 c[8];
  c[0] = glm::vec3(box.min - offset);
  c[1] = glm::vec3(glm::dvec3(box.max.x, box.min.y, box.min.z) - offset);
  c[2] = glm::vec3(glm::dvec3(box.min.x, box.min.y, box.max.z) - offset);
  c[3] = glm::vec3(glm::dvec3(box.max.x, box.min.y, box.max.z) - offset);
  c[4] = glm::vec3(glm::dvec3(box.min.x, box.max.y, box.min.z) - offset);
  c[5] = glm::vec3(glm::dvec3(box.max.x, box.max.y, box.min.z) - offset);
  c[6] = glm::vec3(glm::dvec3(box.min.x, box.max.y, box.max.z) - offset);
  c[7] = glm::vec3(box.max - offset);

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

node_bbox_data_source_t::node_bbox_data_source_t(render::callback_manager_t &a_callbacks)
  : callbacks(a_callbacks)
{
  callbacks.do_create_buffer(camera_buffer, points_buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_buffer, points_type_r32, points_components_4x4, sizeof(camera_matrix), &camera_matrix);
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
  stored_loose_boxes = loose_boxes;
  stored_tight_boxes = tight_boxes;
  line_count = int(stored_loose_boxes.size() + stored_tight_boxes.size()) * 12;

  if (line_count == 0)
    return;

  int vertex_count = line_count * 2;
  int vertex_data_size = int(vertex_count * sizeof(glm::vec3));
  int color_data_size = int(vertex_count * sizeof(glm::u8vec3));

  constexpr int initial_box_count = 200;
  constexpr int vertices_per_box = 24;
  constexpr int initial_vertex_count = initial_box_count * vertices_per_box;

  if (!buffers_created)
  {
    int initial_vertex_capacity = std::max(vertex_data_size,
        int(initial_vertex_count * sizeof(glm::vec3)));
    int initial_color_capacity = std::max(color_data_size,
        int(initial_vertex_count * sizeof(glm::u8vec3)));

    callbacks.do_create_buffer(vertex_buffer, points_buffer_type_vertex);
    callbacks.do_initialize_buffer(vertex_buffer, points_type_r32, points_components_3,
                                   initial_vertex_capacity, nullptr);
    vertex_buffer_capacity = initial_vertex_capacity;

    callbacks.do_create_buffer(color_buffer, points_buffer_type_vertex);
    callbacks.do_initialize_buffer(color_buffer, points_type_u8, points_components_3,
                                   initial_color_capacity, nullptr);
    color_buffer_capacity = initial_color_capacity;

    buffers_created = true;
  }
  else if (vertex_data_size > vertex_buffer_capacity || color_data_size > color_buffer_capacity)
  {
    callbacks.do_initialize_buffer(vertex_buffer, points_type_r32, points_components_3,
                                   vertex_data_size, nullptr);
    vertex_buffer_capacity = vertex_data_size;
    callbacks.do_initialize_buffer(color_buffer, points_type_u8, points_components_3,
                                   color_data_size, nullptr);
    color_buffer_capacity = color_data_size;
  }
}

void node_bbox_data_source_t::add_to_frame(const render::frame_camera_cpp_t &camera, points_to_render_t *to_render)
{
  if (!enabled || line_count <= 0)
    return;

  glm::dvec3 eye = glm::dvec3(camera.inverse_view[3]);

  vertices.clear();
  colors.clear();
  glm::u8vec3 loose_color(255, 255, 0);
  glm::u8vec3 tight_color(0, 255, 255);
  for (auto &box : stored_loose_boxes)
    add_box_edges(vertices, colors, box, loose_color, eye);
  for (auto &box : stored_tight_boxes)
    add_box_edges(vertices, colors, box, tight_color, eye);

  int vertex_data_size = int(vertices.size() * sizeof(vertices[0]));
  int color_data_size = int(colors.size() * sizeof(colors[0]));
  callbacks.do_modify_buffer(vertex_buffer, 0, vertex_data_size, vertices.data());
  callbacks.do_modify_buffer(color_buffer, 0, color_data_size, colors.data());

  glm::dmat4 view_no_trans = camera.view;
  view_no_trans[3] = glm::dvec4(0, 0, 0, 1);
  camera_matrix = glm::mat4(camera.projection * view_no_trans);
  callbacks.do_modify_buffer(camera_buffer, 0, sizeof(camera_matrix), &camera_matrix);

  render_list[0].buffer_mapping = points_node_bbox_bm_camera;
  render_list[0].user_ptr = camera_buffer.user_ptr;
  render_list[1].buffer_mapping = points_node_bbox_bm_position;
  render_list[1].user_ptr = vertex_buffer.user_ptr;
  render_list[2].buffer_mapping = points_node_bbox_bm_color;
  render_list[2].user_ptr = color_buffer.user_ptr;

  points_draw_group_t draw_group = {};
  draw_group.buffers = render_list;
  draw_group.buffers_size = 3;
  draw_group.draw_type = points_node_bbox_lines;
  draw_group.draw_size = line_count * 2;
  points_to_render_add_render_group(to_render, draw_group);
}

} // namespace points::converter
