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

converter_data_source_t::converter_data_source_t(const std::string &url, render::callback_manager_t &callbacks)
  : url(url)
  , processor(url, error)
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

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  (void)to_render;
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  back_buffer = std::make_shared<frustum_tree_walker_t>(camera.view_projection, 3, std::vector<std::string>({std::string("xyz"), std::string("intensity")}));
  processor.walk_tree(back_buffer);
  back_buffer->wait_done();
  auto buffer = back_buffer->m_new_nodes.point_subsets;
  if (buffer.size() == 0)
    return;
}

struct converter_data_source_t *converter_data_source_create(const char *url, uint32_t url_len, error_t *error, struct render::renderer_t *renderer)
{
  if (!error)
    return nullptr;
  auto ret = std::make_unique<converter_data_source_t>(std::string(url, url_len), renderer->callbacks);
  if (ret->error.code != 0)
  {
    *error = ret->error;
    return nullptr;
  }
  return ret.release();
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
