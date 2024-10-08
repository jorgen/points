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
#ifndef POINT_BUFFER_RENDER_HELPER_H
#define POINT_BUFFER_RENDER_HELPER_H

#include "buffer.hpp"
#include "conversion_types.hpp"
#include "storage_handler.hpp"
#include <glm_include.hpp>
#include <points/common/format.h>
#include <points/converter/converter_data_source.h>
#include <points/render/buffer.h>

namespace points::converter
{

struct dyn_points_data_handler_t
{
  dyn_points_data_handler_t(const point_format_t (&point_format)[4])
    : point_format{point_format[0], point_format[1], point_format[2], point_format[3]}
  {
  }

  void start_requests(const std::shared_ptr<dyn_points_data_handler_t> &self, storage_handler_t &storage_handler, const storage_location_t (&locations)[4])
  {
    read_request.reserve(4);
    for (int i = 0; i < 4; i++)
    {
      if (locations[i].offset == 0)
      {
        break;
      }
      target_count++;

      auto set_error = [&self](const error_t &error) {
        if (error.code != 0)
        {
          std::unique_lock<std::mutex> lock(self->mutex);
          if (self->error.code == 0)
          {
            self->error = error;
          }
          self->done++;

          return true;
        }
        return false;
      };
      if (i == 0)
      {
        read_request.emplace_back(storage_handler.read(locations[i], [self, set_error](const storage_handler_request_t &request) {
          if (set_error(request.error))
          {
            return;
          }
          error_t error;
          deserialize_points(request.buffer_info, self->header, self->data_info[0], error);
          if (set_error(error))
          {
            return;
          }

          std::unique_lock<std::mutex> lock(self->mutex);
          self->done++;
        }));
      }
      else
      {
        read_request.emplace_back(storage_handler.read(locations[i], [self, set_error, i](const storage_handler_request_t &request) {
          if (set_error(request.error))
          {
            return;
          }
          self->data_info[i] = request.buffer_info;
          self->done++;
        }));
      }
    }
  }

  bool is_done()
  {
    std::unique_lock<std::mutex> lock(mutex);
    return done == target_count;
  }

  std::vector<std::shared_ptr<read_request_t>> read_request;

  std::mutex mutex;
  int target_count = 0;
  int done = 0;

  error_t error;

  storage_header_t header{};
  point_format_t point_format[4];
  buffer_t data_info[4];
};

struct dyn_points_draw_buffer_t
{
  tree_walker_data_t node_info;
  render::draw_type_t draw_type;
  render::draw_buffer_t render_list[4];
  render::buffer_t render_buffers[3];
  point_format_t format[3];
  std::shared_ptr<uint8_t[]> data[2];
  buffer_t data_info[2];
  uint32_t point_count;
  std::array<double, 3> offset;
  std::array<double, 3> scale;
  glm::mat4 camera_view;
  std::shared_ptr<dyn_points_data_handler_t> data_handler;
  bool rendered = false;
};

template <typename MORTON_TYPE, typename DECODED_T>
void convert_points_to_vertex_data_morton(const tree_config_t &tree_config, const dyn_points_data_handler_t &data_handler, buffer_t &vertex_data_info, std::array<double, 3> &output_offset,
                                          std::shared_ptr<uint8_t[]> &vertex_data)
{
  assert(data_handler.read_request[0]);
  assert(data_handler.data_info[0].size % sizeof(MORTON_TYPE) == 0);
  assert(data_handler.header.point_count == data_handler.data_info[0].size / sizeof(MORTON_TYPE));
  auto *morton_array = static_cast<MORTON_TYPE *>(data_handler.data_info[0].data);
  auto point_count = data_handler.header.point_count;

  auto buffer_size = uint32_t(point_count * sizeof(DECODED_T));
  vertex_data = std::make_shared<uint8_t[]>(buffer_size);
  vertex_data_info = buffer_t(vertex_data.get(), buffer_size);
  auto vertex_data_ptr = vertex_data.get();
  auto *decoded_array = reinterpret_cast<std::array<float, 3> *>(vertex_data_ptr);

  auto mask = morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(data_handler.header.lod_span));

  morton::morton192_t morton_min = morton::morton_and(data_handler.header.morton_min, mask);

  uint64_t min_int[3];
  morton::decode(morton_min, min_int);
  double min[3];
  min[0] = double(min_int[0]) * tree_config.scale;
  min[1] = double(min_int[1]) * tree_config.scale;
  min[2] = double(min_int[2]) * tree_config.scale;

  output_offset[0] = min[0];
  output_offset[1] = min[1];
  output_offset[2] = min[2];

  MORTON_TYPE downcasted_mask = {};
  morton::morton_downcast(mask, downcasted_mask);
  downcasted_mask = morton::morton_negate(downcasted_mask);
  for (uint64_t i = 0; i < point_count; i++)
  {
    uint64_t tmp_pos[3];
    morton::decode(morton::morton_and(morton_array[i], downcasted_mask), tmp_pos);
    for (int n = 0; n < 3; n++)
    {
      decoded_array[i][n] = float(double(tmp_pos[n]) * tree_config.scale);
    }
  }
}

inline void convert_points_to_vertex_data(const tree_config_t &tree_config, const dyn_points_data_handler_t &data_handler, dyn_points_draw_buffer_t &draw_buffer)
{
  assert(data_handler.read_request[0]);
  auto pformat = data_handler.header.point_format;
  auto &point_request = *data_handler.read_request[0];
  switch (pformat.type)
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
  case type_r64: {
    draw_buffer.data[0].reset(new uint8_t[point_request.buffer_info.size]);
    draw_buffer.data_info[0] = buffer_t(draw_buffer.data[0].get(), point_request.buffer_info.size);
    draw_buffer.format[0] = pformat;
    memcpy(draw_buffer.data[0].get(), point_request.buffer_info.data, point_request.buffer_info.size);
    break;
  }
  case type_m32:
    convert_points_to_vertex_data_morton<morton::morton32_t, std::array<uint16_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m64:
    convert_points_to_vertex_data_morton<morton::morton64_t, std::array<uint32_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m128:
    convert_points_to_vertex_data_morton<morton::morton128_t, std::array<uint64_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  case type_m192:
    convert_points_to_vertex_data_morton<morton::morton192_t, std::array<uint64_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(type_r32, components_3);
    break;
  }
}
inline void convert_attribute_to_draw_buffer_data(const dyn_points_data_handler_t &data_handler, dyn_points_draw_buffer_t &draw_buffer, int data_slot)
{
  draw_buffer.draw_type = data_handler.point_format[1].components == components_3 ? render::dyn_points_3 : render::dyn_points_1;
  draw_buffer.data[data_slot] = data_handler.read_request[1]->buffer;
  draw_buffer.data_info[data_slot] = draw_buffer.data_handler->data_info[data_slot];
  draw_buffer.format[data_slot] = draw_buffer.data_handler->point_format[data_slot];
}
} // namespace points::converter

#endif // POINT_BUFFER_RENDER_HELPER_H
