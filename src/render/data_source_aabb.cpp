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
#include "data_source_aabb_p.h"
#include <points/render/aabb_data_source.h>
#include <points/render/buffer.h>

#include "renderer_p.h"

namespace points
{
namespace render
{

template<typename T, int SIZE>
int array_size(const T (&)[SIZE])
{
  return SIZE;
}

static std::vector<glm::vec3> coordinates_for_aabb(const aabb_t &aabb)
{
  std::vector<glm::vec3> coordinates;
  coordinates.resize(8);
  coordinates[0] = glm::vec3(aabb.min[0], aabb.min[1], aabb.min[2]);
  coordinates[1] = glm::vec3(aabb.min[0], aabb.min[1], aabb.max[2]);
  coordinates[2] = glm::vec3(aabb.min[0], aabb.max[1], aabb.min[2]);
  coordinates[3] = glm::vec3(aabb.min[0], aabb.max[1], aabb.max[2]);
  coordinates[4] = glm::vec3(aabb.max[0], aabb.min[1], aabb.min[2]);
  coordinates[5] = glm::vec3(aabb.max[0], aabb.min[1], aabb.max[2]);
  coordinates[6] = glm::vec3(aabb.max[0], aabb.max[1], aabb.min[2]);
  coordinates[7] = glm::vec3(aabb.max[0], aabb.max[1], aabb.max[2]);
  return coordinates;
}

static std::vector<glm::u8vec3> colors_for_aabb()
{
  std::vector<glm::u8vec3> colors;
  colors.resize(8);
  colors[0] = glm::vec3(  0,   0,   0);
  colors[1] = glm::vec3(  0,   0, 255);
  colors[2] = glm::vec3(  0, 255,   0);
  colors[3] = glm::vec3(  0, 255, 255);
  colors[4] = glm::vec3(255,   0,   0);
  colors[5] = glm::vec3(255,   0, 255);
  colors[6] = glm::vec3(255, 255,   0);
  colors[7] = glm::vec3(255, 255, 255);
  return colors;
}

static std::vector<uint16_t> indecies_for_aabb()
{
  std::vector<uint16_t> indecies;
  indecies.reserve(36);
  indecies.push_back(0); indecies.push_back(2); indecies.push_back(4); indecies.push_back(2); indecies.push_back(4); indecies.push_back(6);
  indecies.push_back(4); indecies.push_back(6); indecies.push_back(5); indecies.push_back(5); indecies.push_back(6); indecies.push_back(7);
  indecies.push_back(5); indecies.push_back(7); indecies.push_back(1); indecies.push_back(1); indecies.push_back(7); indecies.push_back(3);
  indecies.push_back(2); indecies.push_back(0); indecies.push_back(3); indecies.push_back(3); indecies.push_back(0); indecies.push_back(1);
  indecies.push_back(5); indecies.push_back(0); indecies.push_back(1); indecies.push_back(5); indecies.push_back(0); indecies.push_back(4);
  indecies.push_back(2); indecies.push_back(3); indecies.push_back(7); indecies.push_back(7); indecies.push_back(6); indecies.push_back(2);
  return indecies;
}

template<typename buffer_data_t>
inline void initialize_buffer(callback_manager_t &callbacks, const std::vector<buffer_data_t> &data_vector, buffer_type_t type, buffer_format_t format, buffer_components_t components, buffer_normalize_t normalize, int buffer_mapping, buffer_t &buffer)
{

  assert(data_vector.size());
  buffer.data = static_cast<const void *>(data_vector.data());
  buffer.data_size = int(data_vector.size() * sizeof(data_vector[0]));
  buffer.type = type;
  buffer.format = format;
  buffer.components = components;
  buffer.normalize = normalize;
  buffer.buffer_mapping = int(buffer_mapping);

  callbacks.do_create_buffer(&buffer);
  callbacks.do_initialize_buffer(&buffer);
}

aabb_data_source_t::aabb_data_source_t(callback_manager_t &callbacks)
  : callbacks(callbacks)
  , project_view(1)
{
  camera_buffer.type = buffer_type_uniform;
  camera_buffer.format = buffer_format_r32;
  camera_buffer.components = component_4x4;
  camera_buffer.normalize = buffer_normalize_do_not_normalize;
  camera_buffer.buffer_mapping = int(aabb_triangle_mesh_camera);
  camera_buffer.data = &project_view;
  camera_buffer.data_size = sizeof(project_view);
  callbacks.do_create_buffer(&camera_buffer);
  callbacks.do_initialize_buffer(&camera_buffer);

  indecies = indecies_for_aabb();
  initialize_buffer(callbacks, indecies, buffer_type_index, buffer_format_u16, component_1, buffer_normalize_do_not_normalize, 0, index_buffer);
  colors = colors_for_aabb();
  initialize_buffer(callbacks, colors, buffer_type_vertex, buffer_format_u8, component_3, buffer_normalize_normalize, aabb_triangle_mesh_color, color_buffer);
}

void aabb_data_source_t::add_to_frame(const frame_camera_t &camera, std::vector<draw_group_t> &to_render)
{
  project_view = camera.view_projection;
  callbacks.do_modify_buffer(&camera_buffer); 
  for (auto &aabb_buffer : aabbs)
  {
    if (!aabb_buffer.vertices_buffer)
    {
      aabb_buffer.vertices_buffer.reset(new buffer_t());
      initialize_buffer(callbacks, aabb_buffer.vertices, buffer_type_t::buffer_type_vertex, buffer_format_t::buffer_format_r32, buffer_components_t::component_3, buffer_normalize_do_not_normalize, aabb_triangle_mesh_position, *aabb_buffer.vertices_buffer.get());
    }
    aabb_buffer.render_list[0].data = aabb_buffer.vertices_buffer.get();
    aabb_buffer.render_list[0].user_ptr = aabb_buffer.vertices_buffer->user_ptr;
    aabb_buffer.render_list[1].data = &index_buffer;
    aabb_buffer.render_list[1].user_ptr = index_buffer.user_ptr;
    aabb_buffer.render_list[2].data = &color_buffer;
    aabb_buffer.render_list[2].user_ptr = color_buffer.user_ptr;
    aabb_buffer.render_list[3].data = &camera_buffer;
    aabb_buffer.render_list[3].user_ptr= camera_buffer.user_ptr;
    draw_group_t draw_group;
    draw_group.buffers = aabb_buffer.render_list;
    draw_group.buffers_size = array_size(aabb_buffer.render_list);
    draw_group.draw_type = draw_type_t::aabb_triangle_mesh;
    draw_group.draw_size = 36;
    to_render.emplace_back(draw_group);
  }
}

struct aabb_data_source_t *aabb_data_source_create(struct renderer_t *renderer)
{
  return new aabb_data_source_t(renderer->callbacks);
}
void aabb_data_source_destroy(struct aabb_data_source_t *aabb_data_source)
{
  delete aabb_data_source;
}
struct data_source_t *aabb_data_source_get(struct aabb_data_source_t *aabb_data_source)
{
  return aabb_data_source;
}

void create_aabb_buffer(const double min[3], const double max[3], aabb_buffer_t &buffer)
{
  memcpy(buffer.aabb.min, min, sizeof(*min) * 3);
  memcpy(buffer.aabb.max, max, sizeof(*max) * 3);
  buffer.vertices = coordinates_for_aabb(buffer.aabb);
}

int aabb_data_source_add_aabb(struct aabb_data_source_t *aabb_data_source, const double min[3], const double max[3])
{
  static uint16_t ids = 0;
  aabb_data_source->aabbs.emplace_back();
  create_aabb_buffer(min, max, aabb_data_source->aabbs.back());
  auto id = ids++;
  aabb_data_source->aabbs_ids.emplace_back(id);
  return id;
}

void aabb_data_source_modify_aabb(struct aabb_data_source_t *aabb_data_source, int id, const double min[3], const double max[3])
{
  auto it = std::find(aabb_data_source->aabbs_ids.begin(), aabb_data_source->aabbs_ids.end(), uint16_t(id));
  if (it == aabb_data_source->aabbs_ids.end())
    return;
  auto i = it - aabb_data_source->aabbs_ids.begin();
  auto aabb_it = aabb_data_source->aabbs.begin() + i;
  auto &aabb_buffer = *aabb_it;
  memcpy(aabb_buffer.aabb.min, min, sizeof(*min) * 3);
  memcpy(aabb_buffer.aabb.max, max, sizeof(*max) * 3);
  aabb_buffer.vertices = coordinates_for_aabb(aabb_buffer.aabb);
}

void aabb_data_source_remove_aabb(struct aabb_data_source_t *aabb_data_source, int id)
{
  auto it = std::find(aabb_data_source->aabbs_ids.begin(), aabb_data_source->aabbs_ids.end(), uint16_t(id));
  if (it == aabb_data_source->aabbs_ids.end())
    return;
  auto i = it - aabb_data_source->aabbs_ids.begin();
  auto aabb_it = aabb_data_source->aabbs.begin() + i;
//  aabb_data_source->to_remove_buffers.emplace_back(
//    std::make_pair(aabb_it->render_list[0], aabb_it->vertices_buffer));
  //auto &to_remove = aabb_data_source->to_remove_buffers.back();
  //to_remove.second.data = nullptr;
  //to_remove.second.data_size = 0;
  aabb_data_source->aabbs_ids.erase(it);
  aabb_data_source->aabbs.erase(aabb_it);
}

void aabb_data_source_get_center(struct aabb_data_source_t *aabb_data_source, int id, double center[3])
{
  auto it = std::find(aabb_data_source->aabbs_ids.begin(), aabb_data_source->aabbs_ids.end(), uint16_t(id));
  if (it == aabb_data_source->aabbs_ids.end())
  {
    memset(center, 0, sizeof(double) * 3);
    return;
  }
  auto i = it - aabb_data_source->aabbs_ids.begin();
  auto aabb_it = aabb_data_source->aabbs.begin() + i;
  auto &aabb_buffer = *aabb_it;
  auto &aabb = aabb_buffer.aabb;
  center[0] = aabb.min[0] + ((aabb.max[0] - aabb.min[0]) / 2);
  center[1] = aabb.min[1] + ((aabb.max[1] - aabb.min[1]) / 2);
  center[2] = aabb.min[2] + ((aabb.max[2] - aabb.min[2]) / 2);
}

}
} // namespace points
