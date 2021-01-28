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
inline void initialize_buffer(callback_manager_t &callbacks, std::vector<buffer_data_t> &data_vector, buffer_type_t type, format_t format, components_t components, buffer_t &buffer)
{
  assert(data_vector.size());
  buffer.releaseBuffer = [&data_vector]() { data_vector = std::vector<buffer_data_t>(); };
  callbacks.do_create_buffer(buffer, type);
  callbacks.do_initialize_buffer(buffer, format, components, int(data_vector.size() * sizeof(data_vector[0])), data_vector.data());
}

aabb_data_source_t::aabb_data_source_t(callback_manager_t &callbacks)
  : callbacks(callbacks)
  , project_view(1)
{
  callbacks.do_create_buffer(project_view_buffer, buffer_type_uniform);
  callbacks.do_initialize_buffer(project_view_buffer, format_r32, components_4x4, sizeof(project_view), &project_view);

  indecies = indecies_for_aabb();
  initialize_buffer(callbacks, indecies, buffer_type_index, format_u16, components_1, index_buffer);
  colors = colors_for_aabb();
  initialize_buffer(callbacks, colors, buffer_type_vertex, format_u8, components_3, color_buffer);
}

void aabb_data_source_t::add_to_frame(const frame_camera_t &camera, std::vector<draw_group_t> &to_render)
{
  project_view = camera.view_projection;
  callbacks.do_modify_buffer(project_view_buffer, 0, sizeof(project_view), &project_view); 
  for (auto &aabb_buffer : aabbs)
  {
    aabb_buffer->render_list[0].buffer_mapping = aabb_bm_position;
    aabb_buffer->render_list[0].user_ptr = aabb_buffer->vertices_buffer.user_ptr;
    aabb_buffer->render_list[1].buffer_mapping = aabb_bm_index;
    aabb_buffer->render_list[1].user_ptr = index_buffer.user_ptr;
    aabb_buffer->render_list[2].buffer_mapping = aabb_bm_color;
    aabb_buffer->render_list[2].user_ptr = color_buffer.user_ptr;
    aabb_buffer->render_list[3].buffer_mapping = aabb_bm_camera;
    aabb_buffer->render_list[3].user_ptr= project_view_buffer.user_ptr;
    draw_group_t draw_group;
    draw_group.buffers = aabb_buffer->render_list;
    draw_group.buffers_size = array_size(aabb_buffer->render_list);
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

void create_aabb_buffer(callback_manager_t &callbacks, const double min[3], const double max[3], aabb_buffer_t *buffer)
{
  memcpy(buffer->aabb.min, min, sizeof(*min) * 3);
  memcpy(buffer->aabb.max, max, sizeof(*max) * 3);
  buffer->vertices = coordinates_for_aabb(buffer->aabb);
  callbacks.do_create_buffer(buffer->vertices_buffer, buffer_type_vertex);
  callbacks.do_initialize_buffer(buffer->vertices_buffer, format_r32, components_3,
                                 int(buffer->vertices.size() * sizeof(buffer->vertices[0])), buffer->vertices.data());
}

int aabb_data_source_add_aabb(struct aabb_data_source_t *aabb_data_source, const double min[3], const double max[3])
{
  static uint16_t ids = 0;
  aabb_data_source->aabbs.emplace_back(new aabb_buffer_t());
  create_aabb_buffer(aabb_data_source->callbacks, min, max, aabb_data_source->aabbs.back().get());
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
  memcpy(aabb_buffer->aabb.min, min, sizeof(*min) * 3);
  memcpy(aabb_buffer->aabb.max, max, sizeof(*max) * 3);
  aabb_buffer->vertices = coordinates_for_aabb(aabb_buffer->aabb);
  aabb_data_source->callbacks.do_modify_buffer(aabb_buffer->vertices_buffer, 0, int(aabb_buffer->vertices.size()),
                                               aabb_buffer->vertices.data());
}

void aabb_data_source_remove_aabb(struct aabb_data_source_t *aabb_data_source, int id)
{
  auto it = std::find(aabb_data_source->aabbs_ids.begin(), aabb_data_source->aabbs_ids.end(), uint16_t(id));
  if (it == aabb_data_source->aabbs_ids.end())
    return;
  auto i = it - aabb_data_source->aabbs_ids.begin();
  auto aabb_it = aabb_data_source->aabbs.begin() + i;
  aabb_data_source->callbacks.do_destroy_buffer((*aabb_it)->vertices_buffer);
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
  auto &aabb = aabb_buffer->aabb;
  center[0] = aabb.min[0] + ((aabb.max[0] - aabb.min[0]) / 2);
  center[1] = aabb.min[1] + ((aabb.max[1] - aabb.min[1]) / 2);
  center[2] = aabb.min[2] + ((aabb.max[2] - aabb.min[2]) / 2);
}

}
} // namespace points
