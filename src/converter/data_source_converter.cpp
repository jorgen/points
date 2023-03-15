/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
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
#include "data_source_converter.hpp"
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>
#include <points/render/buffer.h>

#include "vector_updater.hpp"

#include "renderer.hpp"

namespace points
{
namespace converter
{

template <typename buffer_data_t>
inline void initialize_buffer(render::callback_manager_t &callbacks, std::vector<buffer_data_t> &data_vector, render::buffer_type_t buffer_type, type_t type, components_t components, render::buffer_t &buffer)
{
  assert(data_vector.size());
}

converter_data_source_t::converter_data_source_t(converter_t *converter, render::callback_manager_t &callbacks)
  : converter(converter)
  , callbacks(callbacks)
{
  memset(aabb.min, 0, sizeof(aabb.min));
  memset(aabb.max, 0, sizeof(aabb.max));
  glm::mat4 mat4;
  callbacks.do_create_buffer(project_view_buffer, render::buffer_type_uniform);
  callbacks.do_initialize_buffer(project_view_buffer, type_r32, components_4x4, sizeof(mat4), &mat4);
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr) {
    auto *thiz = static_cast<converter_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };
}

static void combine_buffers(std::vector<tree_walker_nodes_t> &new_nodes, render::callback_manager_t &callback_manager, std::vector<tree_walker_with_buffer_t> (&buffers)[2], bool index)
{
  (void)callback_manager;
  (void)new_nodes;
  (void)buffers;
  (void)index;
  update_vector(new_nodes, buffers[index], buffers[!index], [](const tree_walker_with_buffer_t &a, const tree_walker_nodes_t &b) {
    if (a.node_data.min_morton < b.min_morton)
      return -1;
    if (b.min_morton < a.node_data.min_morton)
      return 1;
    return 0;
  });
}

// static void combine_trees(std::vector<tree_walker_nodes_t> &new_nodes, render::callback_manager_t &callback_manager, std::vector<tree_walker_with_buffer_t> (&buffers)[2], bool index)
//{
//   //update_vector
// }

template <typename MORTON_TYPE, typename DECODED_T>
static void convert_points_to_vertex_data_morton(const read_points_t &read_points, buffer_t &vertex_data_info, std::unique_ptr<uint8_t[]> &vertex_data)
{
  assert(read_points.data.size % sizeof(MORTON_TYPE) == 0);
  assert(read_points.header.public_header.point_count == read_points.data.size / sizeof(MORTON_TYPE));
  MORTON_TYPE *morton_array = static_cast<MORTON_TYPE *>(read_points.data.data);
  auto point_count = read_points.header.public_header.point_count;

  uint64_t buffer_size = point_count * sizeof(DECODED_T);
  vertex_data.reset(new uint8_t[buffer_size]);
  vertex_data_info = buffer_t(vertex_data.get(), buffer_size);
  auto vertex_data_ptr = vertex_data.get();
  DECODED_T *decoded_array = reinterpret_cast<DECODED_T *>(vertex_data_ptr);

  for (uint64_t i = 0; i < point_count; i++)
  {
    morton::decode(morton_array[i], decoded_array[i]);
  }
}

static void convert_points_to_vertex_data(const tree_global_state_t &global_state, const read_points_t &read_points, dyn_points_draw_buffer_t &draw_buffer)
{
  auto pformat = read_points.header.point_format;
  switch (pformat)
  {
  case type_u8:
  case type_i8:
  case type_u16:
  case type_i16:
  case type_u32:
  case type_i32:
  case type_r32:
  case type_u64:
  case type_i64:
  case type_r64:
  {
    draw_buffer.vertex_data.reset(new uint8_t[read_points.data.size]);
    draw_buffer.vertex_data_info = buffer_t(draw_buffer.vertex_data.get(), read_points.data.size);
    draw_buffer.point_type = pformat;
    memcpy(draw_buffer.vertex_data.get(), read_points.data.data, read_points.data.size);
    return;
  }
  case type_m32:
    convert_points_to_vertex_data_morton<morton::morton32_t, std::array<uint16_t, 3>>(read_points, draw_buffer.vertex_data_info, draw_buffer.vertex_data);
    draw_buffer.point_type = points::type_u16;
    return;
  case type_m64:
    convert_points_to_vertex_data_morton<morton::morton64_t, std::array<uint32_t, 3>>(read_points, draw_buffer.vertex_data_info, draw_buffer.vertex_data);
    draw_buffer.point_type = points::type_u32;
    return;
  case type_m128:
    convert_points_to_vertex_data_morton<morton::morton128_t, std::array<uint64_t, 3>>(read_points, draw_buffer.vertex_data_info, draw_buffer.vertex_data);
    draw_buffer.point_type = points::type_u64;
    return;
  case type_m192:
    convert_points_to_vertex_data_morton<morton::morton192_t, std::array<uint64_t, 3>>(read_points, draw_buffer.vertex_data_info, draw_buffer.vertex_data);
    draw_buffer.point_type = points::type_u64;
    return;
  }
}

template<size_t SIZE>
glm::vec<SIZE, glm::f64, glm::defaultp> to_glm(const std::array<double, SIZE> &a)
{
  glm::vec<SIZE, glm::f64, glm::defaultp> ret;
  static_assert(sizeof(ret) == sizeof(a));
  memcpy(&ret, &a, sizeof ret);
  return ret;
}

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  glm::mat4 pv = camera.view_projection;
  callbacks.do_modify_buffer(project_view_buffer, 0, sizeof(pv), &pv);

  if (back_buffer && back_buffer->done())
  {
    aabb = back_buffer->m_tree_aabb;
    std::vector<tree_walker_nodes_t> new_nodes;
    new_nodes.emplace_back(std::move(back_buffer->m_new_nodes));
    combine_buffers(new_nodes, callbacks, current_tree_nodes, current_tree_nodes_index);
    current_tree_nodes_index = !current_tree_nodes_index;
    back_buffer.reset();
  }

  for (int level = 0; level < 5; level++)
  {
    auto &current_trees = current_tree_nodes[current_tree_nodes_index];
    assert(current_trees.size() <= 1);
    for (auto &tree : current_trees)
    {
      assert(tree.buffers[level].size() == tree.node_data.point_subsets[level].size());
      for (int node_index = 0; node_index < int(tree.buffers[level].size()); node_index++)
      {
        auto &node = tree.buffers[level][node_index];
        if (!node.has_data && !node.fetching_data)
        {
          node.has_data = true;
          auto &subsets = tree.node_data.point_subsets[level][node_index];
          auto &buffers = tree.buffers[level][node_index].data;
          buffers.resize(subsets.data.size());
          for (int subset_index = 0; subset_index < int(subsets.data.size()); subset_index++)
          {
            auto &subset = subsets.data[subset_index];
            auto &buffer = buffers[subset_index];
            read_points_t read_points(converter->processor.cache_file(), subset.input_id, 0);
            assert(read_points.data.size);
            convert_points_to_vertex_data(converter->tree_state, read_points, buffer);
            callbacks.do_create_buffer(buffer.render_buffers[0], points::render::buffer_type_vertex);
            callbacks.do_initialize_buffer(buffer.render_buffers[0], read_points.cache_item.header.point_format, points::components_3, read_points.data.size, read_points.data.data);
            callbacks.do_create_buffer(buffer.render_buffers[1], points::render::buffer_type_uniform);
            glm::mat4 mat4;
            callbacks.do_initialize_buffer(buffer.render_buffers[1], type_r32, points::components_4x4, sizeof(mat4), &mat4);
            buffer.render_list[0].buffer_mapping = render::points_bm_vertex;
            buffer.render_list[0].user_ptr = buffer.render_buffers[0].user_ptr;
            buffer.render_list[1].buffer_mapping = render::points_bm_camera;
            buffer.render_list[1].user_ptr = buffer.render_buffers[1].user_ptr;
          }
        }

        if (node.has_data)
        {
          for (auto &buffer : node.data)
          {
            glm::mat4 project_view = camera.projection * glm::translate(camera.view, to_glm(buffer.offset));
            callbacks.do_modify_buffer(buffer.render_buffers[1], 0, sizeof(project_view), &project_view);
            render::draw_group_t draw_group;
            draw_group.buffers = buffer.render_list;
            draw_group.buffers_size = 2;
            draw_group.draw_type = render::dyn_points;
            draw_group.draw_size = buffer.point_count;
            to_render_add_render_group(to_render, draw_group);
          }
        }
      }
    }
  }

  if (!back_buffer)
  {
    back_buffer = std::make_shared<frustum_tree_walker_t>(camera.view_projection);
    converter->processor.walk_tree(back_buffer);
  }
}

struct converter_data_source_t *converter_data_source_create(struct converter::converter_t *converter, struct render::renderer_t *renderer)
{
  return new converter_data_source_t(converter, renderer->callbacks);
}

void converter_data_source_destroy(struct converter_data_source_t *converter_data_source)
{
  delete converter_data_source;
}

struct render::data_source_t converter_data_source_get(struct converter_data_source_t *converter_data_source)
{
  return converter_data_source->data_source;
}

void converter_data_source_get_aabb(struct converter_data_source_t *converter_data_source, double aabb_min[3], double aabb_max[3])
{
  memcpy(aabb_min, converter_data_source->aabb.min, sizeof(converter_data_source->aabb.min));
  memcpy(aabb_max, converter_data_source->aabb.max, sizeof(converter_data_source->aabb.max));
}

} // namespace converter
} // namespace points
