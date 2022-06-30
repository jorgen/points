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
#include <points/render/flat_points_data_source.h>

#include "data_source_flat_points.hpp"

#include "renderer.hpp"

#include <fmt/printf.h>

#include <laszip/laszip_api.h>
namespace points
{
namespace render
{

static glm::vec3 get_point(laszip_point *point, double (&offset)[3], double (&scale)[3], aabb_t &aabb)
{
  glm::dvec3 p(point->X * scale[0] - offset[0], point->Y * scale[1] - offset[1], point->Z * scale[2] - offset[2]);
  if (p[0] < aabb.min[0])
    aabb.min[0] = p[0];
  else if (p[0] > aabb.max[0])
    aabb.max[0] = p[0];
  if (p[1] < aabb.min[1])
    aabb.min[1] = p[1];
  else if (p[1] > aabb.max[1])
    aabb.max[1] = p[1];
  if (p[2] < aabb.min[2])
    aabb.min[2] = p[2];
  else if (p[2] > aabb.max[2])
    aabb.max[2] = p[2];
  return p;
}

flat_points_data_source_t::flat_points_data_source_t(callback_manager_t &callbacks, std::string url)
  : data_source_cpp_t()
  , callbacks(callbacks)
{
  laszip_POINTER laszip_reader;
  if (laszip_create(&laszip_reader))
  {
    fprintf(stderr, "DLL ERROR: creating laszip reader\n");
  }

  laszip_BOOL is_compressed = 0;
  if (laszip_open_reader(laszip_reader, url.c_str(), &is_compressed))
  {
    fprintf(stderr, "DLL ERROR: opening laszip reader for '%s'\n", url.c_str());
  }

  if (!laszip_reader)
  {
    return;
  }

  laszip_header *header;

  if (laszip_get_header_pointer(laszip_reader, &header))
  {
    fprintf(stderr, "DLL ERROR: getting header pointer from laszip reader\n");
  }

  int64_t npoints = (header->number_of_point_records ? header->number_of_point_records : header->extended_number_of_point_records);

  fmt::print(stderr, "file '{}' contains {} points\n", url.c_str(), npoints);

  // get a pointer to the points that will be read

  laszip_point *point;

  if (laszip_get_point_pointer(laszip_reader, &point))
  {
    fprintf(stderr, "DLL ERROR: getting point pointer from laszip reader\n");
  }

  int64_t p_count = 0;

  vertices.reserve(npoints);
  colors.reserve(npoints);
  double offset[3];
  offset[0] = 0.0;
  offset[1] = 0.0;
  offset[2] = 0.0;
  double scale[3];
  scale[0] = header->x_scale_factor;
  scale[1] = header->y_scale_factor;
  scale[2] = header->z_scale_factor;
  
  aabb.min[0] = std::numeric_limits<double>::max();
  aabb.min[1] = std::numeric_limits<double>::max();
  aabb.min[2] = std::numeric_limits<double>::max();
  aabb.max[0] = -std::numeric_limits<double>::max();
  aabb.max[1] = -std::numeric_limits<double>::max();
  aabb.max[2] = -std::numeric_limits<double>::max();
  while (p_count < npoints)
  {
    if (laszip_read_point(laszip_reader))
    {
    }
    vertices.emplace_back(get_point(point, offset, scale, aabb));
    colors.emplace_back(point->rgb[0] >> 8, point->rgb[1] >> 8, point->rgb[2] >> 8);
    p_count++;
  }

  if (laszip_close_reader(laszip_reader))
  {
    fprintf(stderr, "DLL ERROR: closing laszip reader\n");
  }

  // destroy the reader

  if (laszip_destroy(laszip_reader))
  {
    fprintf(stderr, "DLL ERROR: destroying laszip reader\n");
  }

  callbacks.do_create_buffer(vertex_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(vertex_buffer, type_r32, components_3, int(sizeof(*vertices.data()) * vertices.size()), vertices.data());
  
  callbacks.do_create_buffer(color_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(color_buffer, type_u8, components_3, int(sizeof(*colors.data()) * colors.size()), colors.data());

  callbacks.do_create_buffer(project_view_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(project_view_buffer, type_r32, components_4x4, sizeof(project_view), &project_view);

  render_list[0].buffer_mapping = points_bm_camera;
  render_list[0].user_ptr = project_view_buffer.user_ptr;

  render_list[1].buffer_mapping = points_bm_vertex;
  render_list[1].user_ptr = vertex_buffer.user_ptr;

  render_list[2].buffer_mapping = points_bm_color;
  render_list[2].user_ptr = color_buffer.user_ptr;
}
void flat_points_data_source_t::add_to_frame(const frame_camera_cpp_t &camera, to_render_t *to_render)
{
  project_view = camera.view_projection;
  callbacks.do_modify_buffer(project_view_buffer, 0, sizeof(project_view), &project_view);
  draw_group_t draw_group;
  draw_group.buffers = render_list;
  draw_group.buffers_size = sizeof(render_list) / sizeof(*render_list);
  draw_group.draw_type = flat_points;
  draw_group.draw_size = int(vertices.size());
  to_render_add_render_group(to_render, draw_group);
}

struct flat_points_data_source_t *flat_points_data_source_create(struct renderer_t *renderer, const char *url, int url_size)
{
  return new flat_points_data_source_t(renderer->callbacks, std::string(url, url_size));
}
void flat_points_data_source_destroy(struct flat_points_data_source_t *flat_points_data_source)
{
  delete flat_points_data_source;
}
struct data_source_t flat_points_data_source_get(struct flat_points_data_source_t *flat_points_data_source)
{
  return flat_points_data_source->data_source;
}

void flat_points_get_aabb(struct flat_points_data_source_t *points, double aabb_min[3], double aabb_max[3])
{
  memcpy(aabb_min, points->aabb.min, sizeof(points->aabb.min));
  memcpy(aabb_max, points->aabb.max, sizeof(points->aabb.max));
}
}
}
