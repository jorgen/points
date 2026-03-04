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
#include "data_source_origin_anchor.hpp"
#include <points/render/origin_anchor_data_source.h>

#include "renderer.hpp"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace points::render
{

static void generate_arrow(const glm::vec3 &direction, float size, const glm::u8vec4 &color,
                           std::vector<glm::vec3> &vertices, std::vector<glm::u8vec4> &colors, std::vector<uint32_t> &indices)
{
  const int segments = 8;
  float shaft_radius = size * 0.04f;
  float tip_radius = size * 0.10f;
  float shaft_length = size * 0.70f;
  float tip_length = size * 0.30f;

  // Build a coordinate frame where 'forward' is the arrow direction
  glm::vec3 forward = glm::normalize(direction);
  glm::vec3 up = (std::abs(forward.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 right = glm::normalize(glm::cross(forward, up));
  up = glm::cross(right, forward);

  uint32_t base_index = uint32_t(vertices.size());

  // Generate shaft cylinder vertices (2 rings of 'segments' vertices)
  for (int ring = 0; ring < 2; ring++)
  {
    float t = (ring == 0) ? 0.0f : shaft_length;
    for (int i = 0; i < segments; i++)
    {
      float angle = float(2.0 * M_PI * i / segments);
      float c = std::cos(angle);
      float s = std::sin(angle);
      glm::vec3 offset = (right * c + up * s) * shaft_radius;
      vertices.push_back(forward * t + offset);
      colors.push_back(color);
    }
  }

  // Shaft side triangles
  for (int i = 0; i < segments; i++)
  {
    uint32_t i0 = base_index + uint32_t(i);
    uint32_t i1 = base_index + uint32_t((i + 1) % segments);
    uint32_t i2 = base_index + uint32_t(segments + i);
    uint32_t i3 = base_index + uint32_t(segments + (i + 1) % segments);
    indices.push_back(i0);
    indices.push_back(i2);
    indices.push_back(i1);
    indices.push_back(i1);
    indices.push_back(i2);
    indices.push_back(i3);
  }

  // Tip cone: base ring at shaft_length, apex at shaft_length + tip_length
  uint32_t tip_base_index = uint32_t(vertices.size());
  for (int i = 0; i < segments; i++)
  {
    float angle = float(2.0 * M_PI * i / segments);
    float c = std::cos(angle);
    float s = std::sin(angle);
    glm::vec3 offset = (right * c + up * s) * tip_radius;
    vertices.push_back(forward * shaft_length + offset);
    colors.push_back(color);
  }

  // Apex vertex
  uint32_t apex_index = uint32_t(vertices.size());
  vertices.push_back(forward * (shaft_length + tip_length));
  colors.push_back(color);

  // Tip center (for base cap)
  uint32_t tip_center_index = uint32_t(vertices.size());
  vertices.push_back(forward * shaft_length);
  colors.push_back(color);

  // Tip side triangles
  for (int i = 0; i < segments; i++)
  {
    uint32_t i0 = tip_base_index + uint32_t(i);
    uint32_t i1 = tip_base_index + uint32_t((i + 1) % segments);
    indices.push_back(i0);
    indices.push_back(apex_index);
    indices.push_back(i1);
  }

  // Tip base cap triangles
  for (int i = 0; i < segments; i++)
  {
    uint32_t i0 = tip_base_index + uint32_t(i);
    uint32_t i1 = tip_base_index + uint32_t((i + 1) % segments);
    indices.push_back(i1);
    indices.push_back(tip_center_index);
    indices.push_back(i0);
  }
}

origin_anchor_data_source_t::origin_anchor_data_source_t(callback_manager_t &a_callbacks, const glm::dvec3 &a_center, double a_arrow_size)
  : callbacks(a_callbacks)
  , center(a_center)
  , arrow_size(a_arrow_size)
  , camera_matrix(1)
{
  callbacks.do_create_buffer(camera_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(camera_buffer, type_r32, components_4x4, sizeof(camera_matrix), &camera_matrix);

  rebuild_mesh();

  callbacks.do_create_buffer(vertex_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_3, int(vertices.size() * sizeof(vertices[0])), vertices.data());

  callbacks.do_create_buffer(color_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(color_buffer, type_u8, components_4, int(colors.size() * sizeof(colors[0])), colors.data());

  callbacks.do_create_buffer(index_buffer, buffer_type_index);
  callbacks.do_initialize_buffer(index_buffer, type_u32, components_1, int(indices.size() * sizeof(indices[0])), indices.data());
}

void origin_anchor_data_source_t::rebuild_mesh()
{
  vertices.clear();
  colors.clear();
  indices.clear();

  float size = 1.0f; // unit size — actual scale is computed per-frame
  glm::u8vec4 color(160, 160, 160, 128);

  generate_arrow(glm::vec3(+1, 0, 0), size, color, vertices, colors, indices);
  generate_arrow(glm::vec3(-1, 0, 0), size, color, vertices, colors, indices);
  generate_arrow(glm::vec3(0, +1, 0), size, color, vertices, colors, indices);
  generate_arrow(glm::vec3(0, -1, 0), size, color, vertices, colors, indices);
  generate_arrow(glm::vec3(0, 0, +1), size, color, vertices, colors, indices);
  generate_arrow(glm::vec3(0, 0, -1), size, color, vertices, colors, indices);
}

void origin_anchor_data_source_t::add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render)
{
  // Compute scale to maintain constant screen size regardless of distance
  glm::dvec3 eye = glm::dvec3(camera.inverse_view[3]);
  double distance = glm::length(eye - center);
  double screen_fraction = 0.025; // 2.5% of screen height
  double scale = screen_fraction * 2.0 * distance / camera.projection[1][1];

  glm::dmat4 model = glm::translate(glm::dmat4(1.0), center) * glm::scale(glm::dmat4(1.0), glm::dvec3(scale));
  camera_matrix = glm::mat4(camera.projection * camera.view * model);
  callbacks.do_modify_buffer(camera_buffer, 0, sizeof(camera_matrix), &camera_matrix);

  render_list[0].buffer_mapping = origin_anchor_bm_position;
  render_list[0].user_ptr = vertex_buffer.user_ptr;
  render_list[1].buffer_mapping = origin_anchor_bm_color;
  render_list[1].user_ptr = color_buffer.user_ptr;
  render_list[2].buffer_mapping = origin_anchor_bm_camera;
  render_list[2].user_ptr = camera_buffer.user_ptr;
  render_list[3].buffer_mapping = origin_anchor_bm_index;
  render_list[3].user_ptr = index_buffer.user_ptr;

  draw_group_t draw_group = {};
  draw_group.buffers = render_list;
  draw_group.buffers_size = 4;
  draw_group.draw_type = draw_type_t::origin_anchor_mesh;
  draw_group.draw_size = int(indices.size());
  to_render_add_render_group(to_render, draw_group);
}

struct origin_anchor_data_source_t *origin_anchor_data_source_create(struct renderer_t *renderer, const double center[3], double arrow_size)
{
  return new origin_anchor_data_source_t(renderer->callbacks, glm::dvec3(center[0], center[1], center[2]), arrow_size);
}

void origin_anchor_data_source_destroy(struct origin_anchor_data_source_t *anchor)
{
  delete anchor;
}

struct data_source_t origin_anchor_data_source_get(struct origin_anchor_data_source_t *anchor)
{
  return anchor->data_source;
}

void origin_anchor_data_source_set_center(struct origin_anchor_data_source_t *anchor, const double center[3])
{
  anchor->center = glm::dvec3(center[0], center[1], center[2]);
}

void origin_anchor_data_source_set_arrow_size(struct origin_anchor_data_source_t *anchor, double arrow_size)
{
  anchor->arrow_size = arrow_size;
  anchor->rebuild_mesh();
  anchor->callbacks.do_modify_buffer(anchor->vertex_buffer, 0, int(anchor->vertices.size() * sizeof(anchor->vertices[0])), anchor->vertices.data());
  anchor->callbacks.do_modify_buffer(anchor->color_buffer, 0, int(anchor->colors.size() * sizeof(anchor->colors[0])), anchor->colors.data());
  anchor->callbacks.do_modify_buffer(anchor->index_buffer, 0, int(anchor->indices.size() * sizeof(anchor->indices[0])), anchor->indices.data());
}

} // namespace points::render
