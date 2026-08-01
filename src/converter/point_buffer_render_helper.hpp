/************************************************************************
** dewfall - point cloud management software.
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
#include <dew/common/format.h>
#include <dew/converter/converter_data_source.h>
#include <dew/render/buffer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace dew::converter
{

// Rescale an attribute buffer to normalized r32 in [0,1] over [global_min, global_max]. Shared by the stored
// upload path and the virtual-node upload so intensity/scalar attributes get the same contrast stretch (else a
// promoted region drawn in native u16 renders near-black -- a brightness seam vs its neighbours).
inline std::shared_ptr<uint8_t[]> normalize_attribute_to_float(const void *data, uint32_t data_size, dew_type_t type, dew_components_t components,
                                                               uint32_t point_count, double global_min, double global_max, uint32_t &out_size)
{
  double range = global_max - global_min;
  if (range <= 0.0)
    range = 1.0;
  const double inv_range = 1.0 / range;
  const uint32_t comp_count = static_cast<uint32_t>(components);
  out_size = point_count * comp_count * uint32_t(sizeof(float));
  auto result = std::make_shared<uint8_t[]>(out_size);
  auto *dst = reinterpret_cast<float *>(result.get());
  auto *src = static_cast<const uint8_t *>(data);

  int type_size = 1;
  switch (type)
  {
  case dew_type_u8: case dew_type_i8: type_size = 1; break;
  case dew_type_u16: case dew_type_i16: type_size = 2; break;
  case dew_type_u32: case dew_type_i32: case dew_type_r32: type_size = 4; break;
  case dew_type_u64: case dew_type_i64: case dew_type_r64: type_size = 8; break;
  default: type_size = 1; break;
  }
  const uint32_t elem_size = uint32_t(type_size) * comp_count;
  const uint32_t actual_count = std::min(point_count, elem_size ? data_size / elem_size : 0u);
  for (uint32_t i = 0; i < actual_count; i++)
  {
    for (uint32_t c = 0; c < comp_count; c++)
    {
      const uint8_t *elem = src + i * elem_size + c * uint32_t(type_size);
      double val = 0.0;
      switch (type)
      {
      case dew_type_u8:  { uint8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case dew_type_i8:  { int8_t v; memcpy(&v, elem, 1); val = double(v); break; }
      case dew_type_u16: { uint16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case dew_type_i16: { int16_t v; memcpy(&v, elem, 2); val = double(v); break; }
      case dew_type_u32: { uint32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case dew_type_i32: { int32_t v; memcpy(&v, elem, 4); val = double(v); break; }
      case dew_type_r32: { float v; memcpy(&v, elem, 4); val = double(v); break; }
      case dew_type_u64: { uint64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case dew_type_i64: { int64_t v; memcpy(&v, elem, 8); val = double(v); break; }
      case dew_type_r64: { double v; memcpy(&v, elem, 8); val = v; break; }
      default: break;
      }
      float normalized = static_cast<float>((val - global_min) * inv_range);
      normalized = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
      dst[i * comp_count + c] = normalized;
    }
  }
  return result;
}

// Whether an attribute needs the [min,max] contrast stretch. u16x3 (packed RGB) is drawn GL-normalized as-is.
inline bool attribute_should_normalize(dew_type_t type, dew_components_t components, double attr_min, double attr_max)
{
  return (attr_min < attr_max) && !(type == dew_type_u16 && components == dew_components_3);
}

// Draw count for a coarse->fine LOD-ordered node: the prefix down to the finest level its nearest point needs,
// from target on-screen point spacing (render_density_px). Shared by stored + virtual nodes.
inline uint32_t lod_draw_size_from_prefix(const std::array<uint32_t, 64> &prefix_count, uint32_t point_count, double cached_distance, double proj_yy, int viewport_height, double tree_scale, double render_density_px)
{
  if (point_count == 0)
    return 0;
  if (cached_distance <= 0.0 || tree_scale <= 0.0)
    return point_count;
  const double px_per_meter = proj_yy * 0.5 * double(viewport_height) / cached_distance;
  if (px_per_meter <= 0.0)
    return point_count;
  const double cells = (render_density_px / px_per_meter) / tree_scale;
  const int idx = int(std::floor(std::log2(std::max(cells, 1e-9))));
  if (idx < 0)
    return point_count;
  uint32_t draw = prefix_count[size_t(idx > 63 ? 63 : idx)];
  draw = draw < 1 ? 1 : draw;
  return draw > point_count ? point_count : draw;
}

// The storage-free input to the decode (decompress happens upstream). This is the whole contract the CPU
// decode needs -- the owning decompressed buffers + their layout -- with NO dependency on read_request_t,
// the storage handler, or an event loop. It is what makes the decode extractable: it can be built from a
// native read (dyn_points_data_handler_t::as_decode_input) or, in a Web Worker, straight from bytes handed
// over postMessage. `buffers[i]` owns the bytes; `data_info[i]` is the (ptr,size) view into `buffers[i]`.
struct decode_input_t
{
  storage_header_t header{};
  point_format_t point_format[4]{};
  std::shared_ptr<uint8_t[]> buffers[4];
  dew_converter_buffer_t data_info[4]{};
};

struct dyn_points_data_handler_t
{
  dyn_points_data_handler_t(const point_format_t (&a_point_format)[4])
    : point_format{a_point_format[0], a_point_format[1], a_point_format[2], a_point_format[3]}
  {
  }

  // Snapshot the decode inputs (owning buffers + layout) after the reads have completed. The convert/decode
  // routines take this instead of the whole handler, so they carry no storage/loop dependency.
  decode_input_t as_decode_input() const
  {
    decode_input_t in;
    in.header = header;
    for (int i = 0; i < 4; ++i)
    {
      in.point_format[i] = point_format[i];
      in.data_info[i] = data_info[i];
      if (i < int(read_request.size()) && read_request[i])
        in.buffers[i] = read_request[i]->buffer;
    }
    return in;
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
        dew_error_t deser_error;
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

  dew_error_t error;

  storage_header_t header{};
  point_format_t point_format[4];
  dew_converter_buffer_t data_info[4];
};

struct dyn_points_draw_buffer_t
{
  tree_walker_data_t node_info;
  dew_draw_type_t draw_type;
  dew_draw_buffer_t render_list[4];
  dew_buffer_t render_buffers[3];
  point_format_t format[3];
  std::shared_ptr<uint8_t[]> data[2];
  dew_converter_buffer_t data_info[2];
  uint32_t point_count;
  std::array<double, 3> offset;
  std::array<double, 3> scale;
  glm::mat4 camera_view;
  std::shared_ptr<dyn_points_data_handler_t> data_handler;
  size_t gpu_memory_size = 0;
  bool rendered = false;
};

template <typename MORTON_TYPE, typename DECODED_T>
void convert_points_to_vertex_data_morton(const tree_config_t &tree_config, const decode_input_t &in, dew_converter_buffer_t &vertex_data_info, std::array<double, 3> &output_offset,
                                          std::shared_ptr<uint8_t[]> &vertex_data)
{
  assert(in.data_info[0].data);
  assert(in.data_info[0].size % sizeof(MORTON_TYPE) == 0);
  assert(in.header.point_count == in.data_info[0].size / sizeof(MORTON_TYPE));
  auto *morton_array = static_cast<MORTON_TYPE *>(in.data_info[0].data);
  auto point_count = in.header.point_count;

  // Output is always packed float3 (r32x3, 12 bytes); DECODED_T only picks the morton decode width. Sizing
  // by sizeof(DECODED_T) over-allocates (m128/m192) or under-allocates+overflows (m32), so size by float3.
  (void)sizeof(DECODED_T);
  auto buffer_size = uint32_t(point_count * sizeof(std::array<float, 3>));
  vertex_data = std::make_shared<uint8_t[]>(buffer_size);
  vertex_data_info = dew_converter_buffer_t(vertex_data.get(), buffer_size);
  auto vertex_data_ptr = vertex_data.get();
  auto *decoded_array = reinterpret_cast<std::array<float, 3> *>(vertex_data_ptr);

  auto mask = morton::morton_negate(morton::morton_mask_create<uint64_t, 3>(in.header.lod_span));

  morton::morton192_t morton_min = morton::morton_and(in.header.morton_min, mask);

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

inline void convert_points_to_vertex_data(const tree_config_t &tree_config, const decode_input_t &in, dyn_points_draw_buffer_t &draw_buffer)
{
  assert(in.data_info[0].data);
  auto pformat = in.header.point_format;
  switch (pformat.type)
  {
  case dew_type_u8:
  case dew_type_i8:
  case dew_type_u16:
  case dew_type_i16:
  case dew_type_u32:
  case dew_type_i32:
  case dew_type_r32:
  case dew_type_u64:
  case dew_type_i64:
  case dew_type_r64: {
    draw_buffer.data[0].reset(new uint8_t[in.data_info[0].size]);
    draw_buffer.data_info[0] = dew_converter_buffer_t(draw_buffer.data[0].get(), in.data_info[0].size);
    draw_buffer.format[0] = pformat;
    memcpy(draw_buffer.data[0].get(), in.data_info[0].data, in.data_info[0].size);
    break;
  }
  case dew_type_m32:
    convert_points_to_vertex_data_morton<morton::morton32_t, std::array<uint16_t, 3>>(tree_config, in, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(dew_type_r32, dew_components_3);
    break;
  case dew_type_m64:
    convert_points_to_vertex_data_morton<morton::morton64_t, std::array<uint32_t, 3>>(tree_config, in, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(dew_type_r32, dew_components_3);
    break;
  case dew_type_m128:
    convert_points_to_vertex_data_morton<morton::morton128_t, std::array<uint64_t, 3>>(tree_config, in, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(dew_type_r32, dew_components_3);
    break;
  case dew_type_m192:
    convert_points_to_vertex_data_morton<morton::morton192_t, std::array<uint64_t, 3>>(tree_config, in, draw_buffer.data_info[0], draw_buffer.offset, draw_buffer.data[0]);
    draw_buffer.format[0] = point_format_t(dew_type_r32, dew_components_3);
    break;
  }
}
inline void convert_attribute_to_draw_buffer_data(const decode_input_t &in, dyn_points_draw_buffer_t &draw_buffer, int data_slot)
{
  draw_buffer.draw_type = in.point_format[1].components == dew_components_3 ? dew_dyn_points_3 : dew_dyn_points_1;
  draw_buffer.data[data_slot] = in.buffers[data_slot];
  draw_buffer.data_info[data_slot] = in.data_info[data_slot];
  draw_buffer.format[data_slot] = in.point_format[data_slot];
}

// Runtime per-node LOD (Approach B). Points arrive morton-sorted; a point i starts a new grid cell of width
// W iff morton_lod(code[i-1], code[i]) > W. rep_level[i] is that transition level (point 0 is the sentinel,
// always kept). We counting-sort a permutation coarse->fine (highest rep_level first, stable in morton
// order) so that drawing the first prefix_count[W+1] points yields one representative per width-W cell -- a
// screen-uniform subsample. prefix_count[k] = #{ i : rep_level[i] >= k }, so prefix_count[0] == point_count.
constexpr int lod_order_max_level = 63;

// perm orders points coarse->fine; prefix_count[W+1] is the draw count for width W. rep_level_out receives the
// per-point representative level REORDERED to match perm (rep_level_out[j] == rep_level[perm[j]]), so it can be
// uploaded 1:1 with the reordered vertex/attribute buffers and used for a per-point LOD test in the shader.
// Core coarse->fine ordering over an arbitrary morton-sorted array (not tied to a data_handler). Shared by the
// stored-node path (build_lod_order) AND the renderer's virtual-node materialize, so a virtual node's per-point
// LOD ordering + rep_level are produced by the exact same scheme as a stored node's.
template <typename MORTON_TYPE>
inline void build_lod_order_from_mortons(const MORTON_TYPE *morton_array, uint32_t point_count, std::array<uint32_t, 64> &prefix_count, std::vector<uint32_t> &perm, std::vector<uint8_t> &rep_level_out)
{
  perm.resize(point_count);
  rep_level_out.clear();
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

  // Reordered per-point rep_level (coarse->fine, matching perm), for per-point LOD in the shader.
  rep_level_out.resize(point_count);
  for (uint32_t j = 0; j < point_count; j++)
    rep_level_out[j] = rep_level[perm[j]];

  // prefix_count[k] = #{ rep_level >= k } (suffix sum). Draw count for render width W is prefix_count[W+1].
  uint32_t suffix = 0;
  for (int k = lod_order_max_level; k >= 0; k--)
  {
    suffix += hist[k];
    prefix_count[size_t(k)] = suffix;
  }
}

template <typename MORTON_TYPE>
inline void build_lod_order(const dyn_points_data_handler_t &data_handler, std::array<uint32_t, 64> &prefix_count, std::vector<uint32_t> &perm, std::vector<uint8_t> &rep_level_out)
{
  build_lod_order_from_mortons<MORTON_TYPE>(static_cast<const MORTON_TYPE *>(data_handler.data_info[0].data), data_handler.header.point_count, prefix_count, perm, rep_level_out);
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
} // namespace dew::converter

#endif // POINT_BUFFER_RENDER_HELPER_H
