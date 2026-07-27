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
  dyn_points_data_handler_t(const point_format_t (&a_point_format)[4])
    : point_format{a_point_format[0], a_point_format[1], a_point_format[2], a_point_format[3]}
  {
  }

  void cancel_requests()
  {
    for (auto &req : read_request)
    {
      if (req)
        req->set_cancelled();
    }
  }

  void start_requests(const std::shared_ptr<dyn_points_data_handler_t> &self, storage_handler_t &storage_handler, const storage_location_t (&locations)[4])
  {
    (void)self;
    read_request.reserve(4);
    for (int i = 0; i < 4; i++)
    {
      // An empty storage slot is size==0 (the codebase-wide convention); offset==0 is a VALID blob
      // location under object storage (one object per blob), so it must not be treated as empty.
      if (locations[i].size == 0)
      {
        break;
      }
      target_count++;
      read_request.emplace_back(storage_handler.read(locations[i]));
    }
  }

  bool is_done()
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (done == target_count)
      return true;

    // Check if all reads have completed
    for (int i = done; i < target_count; i++)
    {
      auto &req = read_request[i];
      std::unique_lock<std::mutex> req_lock(req->_mutex);
      if (!req->_done)
        return false;
    }

    // All reads complete - process results
    for (int i = 0; i < target_count; i++)
    {
      auto &req = read_request[i];
      if (req->error.code != 0)
      {
        if (error.code == 0)
          error = req->error;
        done++;
        continue;
      }
      if (i == 0)
      {
        points_error_t deser_error;
        deserialize_points(req->buffer_info, header, data_info[0], deser_error);
        if (deser_error.code != 0 && error.code == 0)
          error = deser_error;
      }
      else
      {
        data_info[i] = req->buffer_info;
      }
      done++;
    }
    return done == target_count;
  }

  std::vector<std::shared_ptr<read_request_t>> read_request;

  std::mutex mutex;
  int target_count = 0;
  int done = 0;

  points_error_t error;

  storage_header_t header{};
  point_format_t point_format[4];
  points_converter_buffer_t data_info[4];
};

struct dyn_points_draw_buffer_t
{
  tree_walker_data_t node_info;
  points_draw_type_t draw_type;
  points_draw_buffer_t render_list[4];
  points_buffer_t render_buffers[3];
  point_format_t format[3];
  std::shared_ptr<uint8_t[]> data[2];
  points_converter_buffer_t data_info[2];
  uint32_t point_count;
  std::array<double, 3> offset;
  std::array<double, 3> scale;
  glm::mat4 camera_view;
  std::shared_ptr<dyn_points_data_handler_t> data_handler;
  size_t gpu_memory_size = 0;
  bool rendered = false;
};

template <typename MORTON_TYPE, typename DECODED_T>
void convert_points_to_vertex_data_morton(const tree_config_t &tree_config, const dyn_points_data_handler_t &data_handler, points_converter_buffer_t &vertex_data_info, std::array<double, 3> &output_offset,
                                          std::shared_ptr<uint8_t[]> &vertex_data)
{
  assert(data_handler.read_request[0]);
  assert(data_handler.data_info[0].size % sizeof(MORTON_TYPE) == 0);
  assert(data_handler.header.point_count == data_handler.data_info[0].size / sizeof(MORTON_TYPE));
  auto *morton_array = static_cast<MORTON_TYPE *>(data_handler.data_info[0].data);
  auto point_count = data_handler.header.point_count;

  // Output is always packed float3 (r32x3, 12 bytes); DECODED_T only picks the morton decode width. Sizing
  // by sizeof(DECODED_T) over-allocates (m128/m192) or under-allocates+overflows (m32), so size by float3.
  (void)sizeof(DECODED_T);
  auto buffer_size = uint32_t(point_count * sizeof(std::array<float, 3>));
  vertex_data = std::make_shared<uint8_t[]>(buffer_size);
  vertex_data_info = points_converter_buffer_t(vertex_data.get(), buffer_size);
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
  case points_type_u8:
  case points_type_i8:
  case points_type_u16:
  case points_type_i16:
  case points_type_u32:
  case points_type_i32:
  case points_type_r32:
  case points_type_u64:
  case points_type_i64:
  case points_type_r64: {
    draw_buffer.data[0].reset(new uint8_t[point_request.buffer_info.size]);
    draw_buffer.data_info[0] = points_converter_buffer_t(draw_buffer.data[0].get(), point_request.buffer_info.size);
    draw_buffer.format[0] = pformat;
    memcpy(draw_buffer.data[0].get(), point_request.buffer_info.data, point_request.buffer_info.size);
    break;
  }
  case points_type_m32:
    convert_points_to_vertex_data_morton<morton::morton32_t, std::array<uint16_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(points_type_r32, points_components_3);
    break;
  case points_type_m64:
    convert_points_to_vertex_data_morton<morton::morton64_t, std::array<uint32_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(points_type_r32, points_components_3);
    break;
  case points_type_m128:
    convert_points_to_vertex_data_morton<morton::morton128_t, std::array<uint64_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(points_type_r32, points_components_3);
    break;
  case points_type_m192:
    convert_points_to_vertex_data_morton<morton::morton192_t, std::array<uint64_t, 3>>(tree_config, data_handler, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(points_type_r32, points_components_3);
    break;
  }
}
inline void convert_attribute_to_draw_buffer_data(const dyn_points_data_handler_t &data_handler, dyn_points_draw_buffer_t &draw_buffer, int data_slot)
{
  draw_buffer.draw_type = data_handler.point_format[1].components == points_components_3 ? points_dyn_points_3 : points_dyn_points_1;
  draw_buffer.data[data_slot] = data_handler.read_request[1]->buffer;
  draw_buffer.data_info[data_slot] = draw_buffer.data_handler->data_info[data_slot];
  draw_buffer.format[data_slot] = draw_buffer.data_handler->point_format[data_slot];
}

// Runtime per-node LOD (Approach B). Points arrive morton-sorted; a point i starts a new grid cell of width
// W iff morton_lod(code[i-1], code[i]) > W. rep_level[i] is that transition level (point 0 is the sentinel,
// always kept). We counting-sort a permutation coarse->fine (highest rep_level first, stable in morton
// order) so that drawing the first prefix_count[W+1] points yields one representative per width-W cell -- a
// screen-uniform subsample. prefix_count[k] = #{ i : rep_level[i] >= k }, so prefix_count[0] == point_count.
constexpr int lod_order_max_level = 63;

template <typename MORTON_TYPE>
inline void build_lod_order(const dyn_points_data_handler_t &data_handler, std::array<uint32_t, 64> &prefix_count, std::vector<uint32_t> &perm)
{
  const auto *morton_array = static_cast<const MORTON_TYPE *>(data_handler.data_info[0].data);
  const uint32_t point_count = data_handler.header.point_count;
  perm.resize(point_count);
  prefix_count = {};
  if (point_count == 0)
    return;

  std::vector<uint8_t> rep_level(point_count);
  uint32_t hist[64] = {};
  rep_level[0] = uint8_t(lod_order_max_level); // the first (morton-min) point is a representative at every width
  hist[lod_order_max_level]++;
  for (uint32_t i = 1; i < point_count; i++)
  {
    int level = morton::morton_lod(morton_array[i - 1], morton_array[i]);
    level = level < 0 ? 0 : (level > lod_order_max_level ? lod_order_max_level : level);
    rep_level[i] = uint8_t(level);
    hist[level]++;
  }

  // Counting sort: bucket lod_order_max_level first (coarsest), descending; stable within a bucket.
  uint32_t start[64];
  uint32_t acc = 0;
  for (int level = lod_order_max_level; level >= 0; level--)
  {
    start[level] = acc;
    acc += hist[level];
  }
  for (uint32_t i = 0; i < point_count; i++)
    perm[start[rep_level[i]]++] = i;

  // prefix_count[k] = #{ rep_level >= k } (suffix sum). Draw count for render width W is prefix_count[W+1].
  uint32_t suffix = 0;
  for (int k = lod_order_max_level; k >= 0; k--)
  {
    suffix += hist[k];
    prefix_count[size_t(k)] = suffix;
  }
}

// Reorder a tightly-packed point buffer (stride bytes/point) into permutation order: out[j] = src[perm[j]].
inline std::shared_ptr<uint8_t[]> reorder_points_by_perm(const uint8_t *src, uint32_t point_count, uint32_t stride, const std::vector<uint32_t> &perm)
{
  auto out = std::make_shared<uint8_t[]>(size_t(point_count) * stride);
  auto *out_ptr = out.get();
  for (uint32_t j = 0; j < point_count; j++)
    std::memcpy(out_ptr + size_t(j) * stride, src + size_t(perm[j]) * stride, stride);
  return out;
}
} // namespace points::converter

#endif // POINT_BUFFER_RENDER_HELPER_H
