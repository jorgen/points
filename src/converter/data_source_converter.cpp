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
#include <points/render/buffer.h>
#include <points/converter/converter_data_source.h>

#include "vector_updater.hpp"

#include "renderer.hpp"

namespace points
{
namespace converter
{

template<typename buffer_data_t>
inline void initialize_buffer(render::callback_manager_t &callbacks, std::vector<buffer_data_t> &data_vector, render::buffer_type_t buffer_type, type_t type, components_t components, render::buffer_t &buffer)
{
  assert(data_vector.size());
}

converter_data_source_t::converter_data_source_t(converter_t *converter, render::callback_manager_t &callbacks)
  : converter(converter)
  , callbacks(callbacks)
  , project_view(1)
{
  memset(aabb.min, 0, sizeof(aabb.min));
  memset(aabb.max, 0, sizeof(aabb.max));
  callbacks.do_create_buffer(project_view_buffer, render::buffer_type_uniform);
  callbacks.do_initialize_buffer(project_view_buffer, type_r32, components_4x4, sizeof(project_view), &project_view);
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr)
  {
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

//static void combine_trees(std::vector<tree_walker_nodes_t> &new_nodes, render::callback_manager_t &callback_manager, std::vector<tree_walker_with_buffer_t> (&buffers)[2], bool index)
//{
//  //update_vector
//}

void converter_data_source_t::add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render)
{
  (void)to_render;
  memcpy(&project_view, camera->view_projection, sizeof(project_view));
  callbacks.do_modify_buffer(project_view_buffer, 0, sizeof(project_view), &project_view);

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
    for (auto &tree: current_trees)
    {
      assert(tree.buffers[level].size() == tree.node_data.point_subsets[level].size());
      for (int node_index = 0; node_index < int(tree.buffers[level].size()); node_index++)
      {
        if (tree.buffers[level][node_index].data == nullptr)
        {
          auto &subsets = tree.node_data.point_subsets[level][node_index];
          for (auto &subset : subsets.data)
          {
            read_points_t read_points(converter->processor.cache_file(), subset.input_id, 0);
            assert(read_points.data.size);
          }
        }
      }
    }
  }

  if (!back_buffer)
  {
    back_buffer = std::make_shared<frustum_tree_walker_t>(project_view);
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

}
} // namespace points
