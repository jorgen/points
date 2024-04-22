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
#include "data_source.hpp"
#include "vector_updater.hpp"
#include <array>
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>
#include <points/render/buffer.h>

#include "renderer.hpp"

namespace points
{
namespace converter
{
bool has_rendered = false;
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
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr) {
    auto *thiz = static_cast<converter_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };
}

static void combine_buffers(std::vector<tree_walker_nodes_t> &new_nodes, render::callback_manager_t &callback_manager, std::vector<tree_walker_with_buffer_t> (&buffers)[2], bool index)
{
  (void)callback_manager;
  update_vector(new_nodes, buffers[index], buffers[!index], [](const tree_walker_with_buffer_t &a, const tree_walker_nodes_t &b) {
    if (a.node_data.min_morton < b.min_morton)
      return -1;
    if (b.min_morton < a.node_data.min_morton)
      return 1;
    return 0;
  });
}

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  (void)to_render;
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);

  if (back_buffer && back_buffer->done() && !has_rendered)
  {
    aabb = back_buffer->m_tree_aabb;
    std::vector<tree_walker_nodes_t> new_nodes;
    new_nodes.emplace_back(std::move(back_buffer->m_new_nodes));
    combine_buffers(new_nodes, callbacks, current_tree_nodes, current_tree_nodes_index);
    current_tree_nodes_index = !current_tree_nodes_index;
    back_buffer.reset();
  }

  for (int level = 4; level < 5; level++)
  {
    auto &current_trees = current_tree_nodes[current_tree_nodes_index];
    assert(current_trees.size() <= 1);
    for (auto &tree : current_trees)
    {
      assert(tree.buffers[level].size() == tree.node_data.point_subsets[level].size());
      //      for (int node_index = 0; node_index < int(tree.buffers[level].size()); node_index++)
      //      {
      //        auto &node = tree.buffers[level][node_index];
      //        node.if (!node.&& !node.fetching_data)
      //        {
      //          node.has_data = true;
      //          auto &subset = tree.node_data.point_subsets[level][node_index];
      //          assert(subset.size() == 2);
      //          auto &lod_buffers = tree.buffers[level][node_index].data;
      //          lod_buffers.resize(subset.size());
      //          read_points_t read_points(converter->processor.cache_file(), subset[0].location);
      //          assert(read_points.point_data.size);
      //          convert_points_to_vertex_data(converter->tree_state, read_points, buffer);
      //          callbacks.do_create_buffer(buffer.render_buffers[0], points::render::buffer_type_vertex);
      //          callbacks.do_initialize_buffer(buffer.render_buffers[0], buffer.format[0].first, buffer.format[0].second, int(buffer.data_info[0].size), buffer.data_info[0].data);
      //
      //          read_points_t read_color(converter->processor.cache_file(), subset.input_id, 8);
      //          // auto &attributes = converter->processor.get_attributes(read_points.header.attributes_id);
      //          //(void)attributes;
      //          convert_attribute_to_draw_buffer_data(read_color, buffer, 1);
      //          callbacks.do_create_buffer(buffer.render_buffers[1], points::render::buffer_type_vertex);
      //          callbacks.do_initialize_buffer(buffer.render_buffers[1], buffer.format[1].first, buffer.format[1].second, int(buffer.data_info[1].size), buffer.data_info[1].data);
      //
      //          callbacks.do_create_buffer(buffer.render_buffers[2], points::render::buffer_type_uniform);
      //          callbacks.do_initialize_buffer(buffer.render_buffers[2], type_r32, points::components_4x4, sizeof(buffer.camera_view), &buffer.camera_view);
      //
      //          buffer.render_buffers[0].rendered = true;
      //          buffer.point_count = int(read_points.cache_items.header.point_count);
      //          if (buffer.render_buffers[0].rendered)
      //          {
      //            buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
      //            buffer.render_list[0].user_ptr = buffer.render_buffers[0].user_ptr;
      //            buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
      //            buffer.render_list[1].user_ptr = buffer.render_buffers[1].user_ptr;
      //            buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
      //            buffer.render_list[2].user_ptr = buffer.render_buffers[2].user_ptr;
      //          }
      //        }
      //      }
      //
      //      if (node.has_data)
      //      {
      //        for (auto &buffer : node.data)
      //        {
      //          if (!buffer.render_buffers[0].rendered)
      //            continue;
      //          buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(buffer.offset));
      //          callbacks.do_modify_buffer(buffer.render_buffers[2], 0, sizeof(buffer.camera_view), &buffer.camera_view);
      //          render::draw_group_t draw_group;
      //          draw_group.buffers = buffer.render_list;
      //          draw_group.buffers_size = 3;
      //          draw_group.draw_type = render::dyn_points;
      //          draw_group.draw_size = buffer.point_count;
      //          to_render_add_render_group(to_render, draw_group);
      //          has_rendered = true;
      //        }
      //}
    }
  }
  //}
  // if (!back_buffer && !has_rendered)
  //{
  //   back_buffer = std::make_shared<frustum_tree_walker_t>(camera.view_projection);
  //   converter->processor.walk_tree(back_buffer);
  // }
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
