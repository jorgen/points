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

void converter_data_source_t::add_to_frame(render::frame_camera_t *camera, render::to_render_t *to_render)
{
  //tree_walker.
  //tree_walker.walk_tree()
  memcpy(&project_view, camera->view_projection, sizeof(project_view));
  callbacks.do_modify_buffer(project_view_buffer, 0, sizeof(project_view), &project_view);

  if (back_buffer && back_buffer->done())
  {
    aabb = back_buffer->tree_aabb;
    back_buffer.reset();
  }

  if (!back_buffer)
  {
    back_buffer = std::make_shared<frustum_tree_walker_t>(project_view);
    converter->processor.walkt_tree(back_buffer);
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
