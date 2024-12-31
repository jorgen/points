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
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>

#include <fmt/printf.h>

#include "renderer.hpp"

namespace points::converter
{
bool has_rendered = false;
template <typename buffer_data_t>
void initialize_buffer(render::callback_manager_t &callbacks, std::vector<buffer_data_t> &data_vector, render::buffer_type_t buffer_type, type_t type, components_t components, render::buffer_t &buffer)
{
  assert(data_vector.size());
}

converter_data_source_t::converter_data_source_t(const std::string &url, render::callback_manager_t &callbacks)
  : url(url)
  , processor(url, file_existence_requirement_t::exist, error)
  , callbacks(callbacks)
{
  if (error.code != 0)
  {
    return;
  }
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr) {
    auto *thiz = static_cast<converter_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };

  if (processor.attrib_name_registry_count() > 2)
  {
    char buffer[256];
    auto str_size = processor.attrib_name_registry_get(1, buffer, sizeof(buffer));
    next_attribute_name.assign(buffer, str_size);
  }
}

bool less_than(const tree_walker_data_t &lhs, const tree_walker_data_t &rhs)
{
  if (lhs.lod == rhs.lod)
  {
    auto node_equals = lhs.node <=> rhs.node;
    if (node_equals == std::strong_ordering::equal)
    {
      return lhs.input_id < rhs.input_id;
    }
    return node_equals == std::strong_ordering::less;
  }
  return lhs.lod < rhs.lod;
}

static void add_request_to_dynbuffer(storage_handler_t &storage_handler, dyn_points_draw_buffer_t &buffer, const tree_walker_data_t &node)
{
  buffer.node_info = node;
  buffer.data_handler = std::make_shared<dyn_points_data_handler_t>(node.format);
  buffer.data_handler->start_requests(buffer.data_handler, storage_handler, node.locations);
}

void converter_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  (void)to_render;
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);
  bool new_attribute = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    new_attribute = current_attribute_name != next_attribute_name;
    current_attribute_name = next_attribute_name;
  }
  if (new_attribute)
  {
    render_buffers.clear();
  }
  back_buffer = std::make_shared<frustum_tree_walker_t>(camera.view_projection, 7, std::vector<std::string>({std::string("xyz"), current_attribute_name}));
  auto copy_back_buffer_ptr = back_buffer;
  processor.walk_tree(std::move(copy_back_buffer_ptr));
  back_buffer->wait_done();
  auto &buffer = back_buffer->m_new_nodes.point_subsets;
  std::sort(buffer.begin(), buffer.end(), less_than);

  std::vector<std::unique_ptr<dyn_points_draw_buffer_t>> new_render_buffers;
  new_render_buffers.reserve(buffer.size());

  auto render_buffers_it = render_buffers.begin();
  int i;
  for (i = 0; i < int(buffer.size()) && render_buffers_it != render_buffers.end(); i++)
  {
    auto &node = buffer[i];
    while (render_buffers_it != render_buffers.end() && less_than(render_buffers_it->get()->node_info, node))
    {
      render_buffers_it++;
    }
    if (render_buffers_it == render_buffers.end())
    {
      break;
    }
    if (less_than(node, render_buffers_it->get()->node_info))
    {
      auto &new_buffer = new_render_buffers.emplace_back(new dyn_points_draw_buffer_t());
      add_request_to_dynbuffer(processor.storage_handler(), *new_buffer, node);
    }
    else
    {
      new_render_buffers.emplace_back(std::move(*render_buffers_it));
      render_buffers_it++;
    }
  }
  for (; i < int(buffer.size()); i++)
  {
    auto &node = buffer[i];
    auto &new_buffer = new_render_buffers.emplace_back(new dyn_points_draw_buffer_t());
    add_request_to_dynbuffer(processor.storage_handler(), *new_buffer, node);
  }

  render_buffers = std::move(new_render_buffers);
  auto tree_config = processor.tree_config();
  for (auto &render_buffer_ptr : render_buffers)
  {
    assert(render_buffer_ptr);
    auto &render_buffer = *render_buffer_ptr;
    if (render_buffer.data_handler)
    {
      if (render_buffer.data_handler->is_done())
      {
        render_buffer.point_count = render_buffer.data_handler->header.point_count;
        convert_points_to_vertex_data(tree_config, *render_buffer.data_handler, render_buffer);
        callbacks.do_create_buffer(render_buffer.render_buffers[0], points::render::buffer_type_vertex);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[0], render_buffer.format[0].type, render_buffer.format[0].components, int(render_buffer.data_info[0].size), render_buffer.data_info[0].data);

        convert_attribute_to_draw_buffer_data(*render_buffer.data_handler, render_buffer, 1);
        callbacks.do_create_buffer(render_buffer.render_buffers[1], points::render::buffer_type_vertex);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[1], render_buffer.format[1].type, render_buffer.format[1].components, int(render_buffer.data_info[1].size), render_buffer.data_info[1].data);

        render_buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(render_buffer.offset));

        callbacks.do_create_buffer(render_buffer.render_buffers[2], points::render::buffer_type_uniform);
        callbacks.do_initialize_buffer(render_buffer.render_buffers[2], type_r32, points::components_4x4, sizeof(render_buffer.camera_view), &render_buffer.camera_view);

        render_buffer.render_list[0].buffer_mapping = render::dyn_points_bm_vertex;
        render_buffer.render_list[0].user_ptr = render_buffer.render_buffers[0].user_ptr;
        render_buffer.render_list[1].buffer_mapping = render::dyn_points_bm_color;
        render_buffer.render_list[1].user_ptr = render_buffer.render_buffers[1].user_ptr;
        render_buffer.render_list[2].buffer_mapping = render::dyn_points_bm_camera;
        render_buffer.render_list[2].user_ptr = render_buffer.render_buffers[2].user_ptr;
        render_buffer.rendered = true;
        render_buffer.data_handler.reset();
      }
      else
      {
        continue;
      }
    }
    assert(render_buffer.rendered);
    auto offset = to_glm(tree_config.offset) + to_glm(render_buffer.offset);
    render_buffer.camera_view = camera.projection * glm::translate(camera.view, offset);
    callbacks.do_modify_buffer(render_buffer.render_buffers[2], 0, sizeof(render_buffer.camera_view), &render_buffer.camera_view);
    render::draw_group_t draw_group = {render_buffer.draw_type, render_buffer.render_list, 3, int(render_buffer.point_count)};
    to_render_add_render_group(to_render, draw_group);
  }
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

void converter_data_source_request_aabb(struct converter_data_source_t *converter_data_source, converter_data_source_request_aabb_callback_t callback, void *user_ptr)
{
  auto callback_cpp = [callback, user_ptr](double aabb_min[3], double aabb_max[3]) { callback(aabb_min, aabb_max, user_ptr); };

  converter_data_source->processor.request_aabb(callback_cpp);
}

uint32_t converter_data_attribute_count(struct converter_data_source_t *converter_data_source)
{
  return converter_data_source->processor.attrib_name_registry_count();
}

uint32_t converter_data_get_attribute_name(struct converter_data_source_t *converter_data_source, int index, char *name, uint32_t name_size)
{
  return converter_data_source->processor.attrib_name_registry_get(index, name, name_size);
}
void converter_data_set_rendered_attribute(struct converter_data_source_t *converter_data_source, const char *name, uint32_t name_len)
{
  std::unique_lock<std::mutex> lock(converter_data_source->mutex);
  converter_data_source->next_attribute_name.assign(name, name_len);
}

} // namespace points::converter
