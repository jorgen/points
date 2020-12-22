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

namespace points
{
namespace render
{

template<typename T, int SIZE>
int array_size(const T (&)[SIZE])
{
  return SIZE;
}

static std::vector<glm::vec3> coordinates_for_aabb(const aabb &aabb)
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

template<typename buffer_t>
inline void initialize_buffer(buffer &render_buffer, const std::vector<buffer_t> &data_vector, std::unique_ptr<buffer_data> &buffer_data, buffer_type buffer_type, buffer_format buffer_format, buffer_components components, buffer_data_normalize normalize, int buffer_mapping)
{
  if (!buffer_data)
    buffer_data.reset(new points::render::buffer_data());
  if (buffer_data->state == buffer_data::state::add || buffer_data->state == buffer_data::state::modify)
  {
    if (data_vector.size())
    {
      buffer_data->data = static_cast<const void *>(data_vector.data());
      buffer_data->data_size = int(data_vector.size() * sizeof(data_vector[0]));
    } 
    else
    {
      buffer_data->data = nullptr;
      buffer_data->data_size = int(0);
    }
    buffer_data->rendered = false;
    render_buffer = {buffer_type, buffer_format, components, normalize, buffer_mapping, buffer_data.get(), &(buffer_data->user_ptr)};
  }
}

template<typename buffer_t>
static void add_buffers_to_renderlist(buffer &render_buffer, std::vector<buffer_t> &data_vector, std::unique_ptr<buffer_data> &buffer_data, std::vector<buffer> &to_add, std::vector<buffer> &to_update)
{
  assert(buffer_data);
  if (buffer_data->state == buffer_data::state::add)
  {
    to_add.emplace_back(render_buffer);
  }
  else if (buffer_data->state == buffer_data::state::modify)
  {
    to_update.emplace_back(render_buffer);
  }
  else if (buffer_data->rendered)
  {
    buffer_data->data = nullptr;
    buffer_data->data_size = 0;
    data_vector.clear();
  }
  buffer_data->state = buffer_data::state::render;
}

aabb_data_source::aabb_data_source()
{
  indecies = indecies_for_aabb();
  initialize_buffer(index_buffer, indecies, index_buffer_data, buffer_type_index, u16, component_1, do_not_normalize, 0);
  colors = colors_for_aabb();
  initialize_buffer(color_buffer, colors, color_buffer_data, buffer_type_vertex, u8, component_3, normalize, aabb_triangle_mesh_color);
}

void aabb_data_source::add_to_frame(const renderer &renderer, const camera &camera, std::vector<buffer> &to_add, std::vector<buffer> &to_update, std::vector<buffer> &to_remove, std::vector<draw_group> &to_render)
{
  (void)renderer;
  (void)camera;
  (void)to_remove;
 
  for (auto &aabb_buffer : aabbs)
  {
    initialize_buffer(aabb_buffer.render_list[0], aabb_buffer.vertices, aabb_buffer.vertices_buffer_data, buffer_type::buffer_type_vertex, buffer_format::r32, buffer_components::component_3, do_not_normalize, aabb_triangle_mesh_position);
    add_buffers_to_renderlist(aabb_buffer.render_list[0], aabb_buffer.vertices, aabb_buffer.vertices_buffer_data, to_add, to_update);
    aabb_buffer.render_list[1] = index_buffer;
    add_buffers_to_renderlist(aabb_buffer.render_list[1], indecies, index_buffer_data, to_add, to_update);
    aabb_buffer.render_list[2] = color_buffer;
    add_buffers_to_renderlist(aabb_buffer.render_list[2], colors, color_buffer_data, to_add, to_update);
    draw_group draw_group;
    draw_group.buffers = aabb_buffer.render_list;
    draw_group.buffers_size = array_size(aabb_buffer.render_list);
    draw_group.draw_type = draw_type::aabb_triangle_mesh;
    draw_group.draw_size = 36;
    to_render.emplace_back(draw_group);
  }
}

struct aabb_data_source *aabb_data_source_create()
{
  return new aabb_data_source();
}
void aabb_data_source_destroy(struct aabb_data_source *aabb_data_source)
{
  delete aabb_data_source;
}
struct data_source *aabb_data_source_get(struct aabb_data_source *aabb_data_source)
{
  return aabb_data_source;
}

void create_aabb_buffer(const double min[3], const double max[3], aabb_buffer &buffer)
{
  memcpy(buffer.aabb.min, min, sizeof(*min) * 3);
  memcpy(buffer.aabb.max, max, sizeof(*max) * 3);
  buffer.vertices = coordinates_for_aabb(buffer.aabb);
}

int aabb_data_source_add_aabb(struct aabb_data_source *aabb_data_source, const double min[3], const double max[3])
{
  static uint16_t ids = 0;
  aabb_data_source->aabbs.emplace_back();
  create_aabb_buffer(min, max, aabb_data_source->aabbs.back());
  auto id = ids++;
  aabb_data_source->aabbs_ids.emplace_back(id);
  return id;
}

void aabb_data_source_modify_aabb(struct aabb_data_source *aabb_data_source, int id, const double min[3], const double max[3])
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

void aabb_data_source_remove_aabb(struct aabb_data_source *aabb_data_source, int id)
{
  auto it = std::find(aabb_data_source->aabbs_ids.begin(), aabb_data_source->aabbs_ids.end(), uint16_t(id));
  if (it == aabb_data_source->aabbs_ids.end())
    return;
  auto i = it - aabb_data_source->aabbs_ids.begin();
  auto aabb_it = aabb_data_source->aabbs.begin() + i;
//  aabb_data_source->to_remove_buffers.emplace_back(
//    std::make_pair(aabb_it->render_list[0], aabb_it->vertices_buffer_data));
  auto &to_remove = aabb_data_source->to_remove_buffers.back();
  to_remove.second.data = nullptr;
  to_remove.second.data_size = 0;
  aabb_data_source->aabbs_ids.erase(it);
  aabb_data_source->aabbs.erase(aabb_it);
}

void aabb_data_source_get_center(struct aabb_data_source *aabb_data_source, int id, double center[3])
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
