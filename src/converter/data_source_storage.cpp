/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2023  Jørgen Lind
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
#include "data_source_storage.hpp"

#include "data_source.hpp"
#include "renderer.hpp"

namespace points
{
namespace converter
{
storage_data_source_t::storage_data_source_t(converter_t *converter, render::callback_manager_t &callback_manager)
  : converter(converter)
  , callbacks(callback_manager)
{
  data_source.user_ptr = this;
  data_source.add_to_frame = [](render::frame_camera_t *camera, render::to_render_t *to_render, void *user_ptr) {
    auto *thiz = static_cast<storage_data_source_t *>(user_ptr);
    thiz->add_to_frame(camera, to_render);
  };
}

void storage_data_source_t::add_to_frame(render::frame_camera_t *c_camera, render::to_render_t *to_render)
{
  const render::frame_camera_cpp_t camera = render::cast_to_frame_camera_cpp(*c_camera);

  if (render_item.buffer.vertex_data)
  {
      render_item.buffer.camera_view = camera.projection * glm::translate(camera.view, to_glm(render_item.buffer.offset));
      callbacks.do_modify_buffer(render_item.buffer.render_buffers[1], 0, sizeof(render_item.buffer.camera_view), &render_item.buffer.camera_view);
      render::draw_group_t draw_group;
      draw_group.buffers = render_item.buffer.render_list;
      draw_group.buffers_size = 2;
      draw_group.draw_type = render::dyn_points;
      draw_group.draw_size = render_item.buffer.point_count;
      to_render_add_render_group(to_render, draw_group);
  }
}

void storage_data_source_t::set_item(uint32_t id, uint32_t sub)
{
  input_data_id_t input_id = {id, sub};
  read_points_t read_points(converter->processor.cache_file(), input_id, 0);
  assert(read_points.data.size);
  convert_points_to_vertex_data(converter->tree_state, read_points, render_item.buffer);
  callbacks.do_create_buffer(render_item.buffer.render_buffers[0], points::render::buffer_type_vertex);
  callbacks.do_initialize_buffer(render_item.buffer.render_buffers[0], points::type_r32, points::components_3, int(render_item.buffer.vertex_data_info.size), render_item.buffer.vertex_data_info.data);
  render_item.buffer.render_buffers[0].rendered = true;
  render_item.buffer.point_count = int(read_points.cache_item.header.public_header.point_count);
  callbacks.do_create_buffer(render_item.buffer.render_buffers[1], points::render::buffer_type_uniform);
  callbacks.do_initialize_buffer(render_item.buffer.render_buffers[1], type_r32, points::components_4x4, sizeof(render_item.buffer.camera_view), &render_item.buffer.camera_view);
  render_item.buffer.render_list[0].buffer_mapping = render::points_bm_vertex;
  render_item.buffer.render_list[0].user_ptr = render_item.buffer.render_buffers[0].user_ptr;
  render_item.buffer.render_list[1].buffer_mapping = render::points_bm_camera;
  render_item.buffer.render_list[1].user_ptr = render_item.buffer.render_buffers[1].user_ptr;
  render_item.id = id;
  render_item.sub = sub;

}

int storage_data_source_t::get_items(uint32_t **input_ids, uint32_t **input_subs, int buffer_size)
{
  return converter->processor.cache_file().fill_ids(input_ids, input_subs, buffer_size);
}

int storage_data_source_t::item_count()
{
  return converter->processor.cache_file().item_count();
}

struct storage_data_source_t *storage_data_source_create(struct converter::converter_t *converter, struct render::renderer_t *renderer)
{
  return new storage_data_source_t(converter, renderer->callbacks);
}

void storage_data_source_destroy(struct storage_data_source_t *storage_data_source)
{
  delete storage_data_source;
}

struct render::data_source_t storage_data_source_get(struct storage_data_source_t *storage_data_source)
{
  return storage_data_source->data_source;
}

int storage_data_source_ids(struct storage_data_source_t *storage_data_source, uint32_t **id_buffer, uint32_t **sub_id_buffer, int buffer_size)
{
  return storage_data_source->get_items(id_buffer, sub_id_buffer, buffer_size);
}

int storage_data_source_ids_count(struct storage_data_source_t *storage_data_source)
{
  return storage_data_source->item_count();
}
void storage_data_source_render(struct storage_data_source_t *storage_data_source, uint32_t id_buffer, uint32_t sub_id_buffer)
{
  storage_data_source->set_item(id_buffer, sub_id_buffer);
}

}
}
