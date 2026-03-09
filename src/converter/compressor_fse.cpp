/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
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
#include "compressor_fse.hpp"
#include "byte_shuffle.hpp"
#include "compression_preprocess.hpp"
#include "input_header.hpp"

extern "C" {
#include <common/huf.h>
}

#include <cstring>
#include <vector>

namespace points::converter
{

static constexpr uint32_t huf_max_block_size = HUF_BLOCKSIZE_MAX; // 128 KB

struct huf_compress_result_t
{
  std::vector<uint8_t> data;
  uint32_t size;
  points_error_t error;
};

static huf_compress_result_t huf_compress_chunked(const uint8_t *src, uint32_t src_size)
{
  huf_compress_result_t r;
  r.size = 0;

  uint32_t chunk_count = (src_size + huf_max_block_size - 1) / huf_max_block_size;
  if (src_size == 0)
    chunk_count = 0;

  uint32_t max_output = sizeof(uint32_t) + chunk_count * (2 * sizeof(uint32_t) + HUF_COMPRESSBOUND(huf_max_block_size));
  r.data.resize(max_output);
  auto *out_ptr = r.data.data();

  memcpy(out_ptr, &chunk_count, sizeof(uint32_t));
  out_ptr += sizeof(uint32_t);
  uint32_t total = sizeof(uint32_t);

  alignas(8) uint64_t workspace[HUF_WORKSPACE_SIZE_U64];
  HUF_CElt huf_table[HUF_CTABLE_SIZE_ST(HUF_SYMBOLVALUE_MAX)];

  for (uint32_t c = 0; c < chunk_count; c++)
  {
    uint32_t chunk_offset = c * huf_max_block_size;
    uint32_t chunk_size = (c + 1 == chunk_count) ? (src_size - chunk_offset) : huf_max_block_size;

    size_t max_chunk_compressed = HUF_COMPRESSBOUND(chunk_size);
    uint8_t *chunk_data_ptr = out_ptr + 2 * sizeof(uint32_t);

    HUF_repeat repeat = HUF_repeat_none;
    size_t compressed_chunk_size = HUF_compress4X_repeat(
      chunk_data_ptr, max_chunk_compressed,
      src + chunk_offset, chunk_size,
      HUF_SYMBOLVALUE_MAX, HUF_TABLELOG_DEFAULT,
      workspace, sizeof(workspace),
      huf_table, &repeat, 0);

    if (HUF_isError(compressed_chunk_size))
    {
      r.error.code = -1;
      r.error.msg = std::string("HUF_compress4X_repeat failed: ") + HUF_getErrorName(compressed_chunk_size);
      return r;
    }

    uint32_t uncompressed_size_field = chunk_size;
    uint32_t compressed_size_field;

    if (compressed_chunk_size == 0 || compressed_chunk_size >= chunk_size)
    {
      compressed_size_field = chunk_size;
      memcpy(chunk_data_ptr, src + chunk_offset, chunk_size);
      compressed_chunk_size = chunk_size;
    }
    else
    {
      compressed_size_field = static_cast<uint32_t>(compressed_chunk_size);
    }

    memcpy(out_ptr, &uncompressed_size_field, sizeof(uint32_t));
    memcpy(out_ptr + sizeof(uint32_t), &compressed_size_field, sizeof(uint32_t));
    uint32_t frame_size = 2 * sizeof(uint32_t) + static_cast<uint32_t>(compressed_chunk_size);
    out_ptr += frame_size;
    total += frame_size;
  }

  r.data.resize(total);
  r.size = total;
  return r;
}

compression_method_t compressor_huff0_t::method() const
{
  return compression_method_t::huff0;
}

static void huf_compress_standard(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count, uint8_t flags_in,
                                  std::vector<uint8_t> &working, compression_result_t &result)
{
  int typesize = size_for_format(format.type);
  if (typesize <= 0)
    typesize = 1;

  uint8_t element_stride = static_cast<uint8_t>(typesize * static_cast<int>(format.components));
  uint8_t flags = flags_in;

  uint32_t data_bytes = static_cast<uint32_t>(point_count) * static_cast<uint32_t>(element_stride);
  uint32_t data_offset = (data_bytes > 0 && data_bytes <= size) ? (size - data_bytes) : 0;

  if (working.empty())
    working.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);

  if (data_bytes > 0 && delta_encode_morton(working.data() + data_offset, data_bytes, element_stride))
    flags |= compression_flag_delta_encoded;

  std::vector<uint8_t> shuffled(size);
  byte_shuffle(working.data(), shuffled.data(), size, static_cast<uint32_t>(typesize), static_cast<uint32_t>(format.components));

  uint32_t delta_meta_size = (flags & compression_flag_delta_encoded) ? sizeof(uint32_t) : 0;

  auto compressed_full = huf_compress_chunked(shuffled.data(), size);
  if (compressed_full.error.code != 0)
  {
    result.error = compressed_full.error;
    return;
  }

  auto band_result = detect_constant_bands(shuffled.data(), size, static_cast<uint8_t>(typesize), static_cast<uint8_t>(format.components));

  bool use_bands = false;
  huf_compress_result_t compressed_compacted;
  uint32_t band_meta_size = 0;

  if (band_result.band_mask != 0)
  {
    band_meta_size = sizeof(uint32_t) + static_cast<uint32_t>(band_result.constant_values.size());
    uint32_t compacted_size = static_cast<uint32_t>(band_result.compacted_data.size());

    compressed_compacted = huf_compress_chunked(band_result.compacted_data.data(), compacted_size);

    if (compressed_compacted.error.code == 0)
    {
      uint32_t total_with_bands = delta_meta_size + band_meta_size + compressed_compacted.size;
      uint32_t total_without_bands = delta_meta_size + compressed_full.size;
      if (total_with_bands < total_without_bands)
      {
        use_bands = true;
      }
    }
  }

  uint32_t payload_size;
  if (use_bands)
  {
    flags |= compression_flag_constant_bands;
    payload_size = delta_meta_size + band_meta_size + compressed_compacted.size;
  }
  else
  {
    payload_size = delta_meta_size + compressed_full.size;
  }

  compression_header_t header;
  header.magic[0] = 'P';
  header.magic[1] = 'C';
  header.magic[2] = 'M';
  header.magic[3] = 1;
  header.type_size = static_cast<uint8_t>(typesize);
  header.component_count = static_cast<uint8_t>(format.components);
  header.uncompressed_size = size;

  if (payload_size >= size)
  {
    header.method = compression_method_t::none;
    header.flags = 0;
    header.compressed_size = size;
    uint32_t total = static_cast<uint32_t>(sizeof(header)) + size;
    auto output = std::make_shared<uint8_t[]>(total);
    memcpy(output.get(), &header, sizeof(header));
    memcpy(output.get() + sizeof(header), data, size);
    result.data = std::move(output);
    result.size = total;
  }
  else
  {
    header.method = compression_method_t::huff0;
    header.flags = flags;
    header.compressed_size = payload_size;
    uint32_t total = static_cast<uint32_t>(sizeof(header)) + payload_size;
    auto output = std::make_shared<uint8_t[]>(total);
    memcpy(output.get(), &header, sizeof(header));

    uint8_t *ptr = output.get() + sizeof(header);
    if (flags & compression_flag_delta_encoded)
    {
      memcpy(ptr, &data_offset, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
    }
    if (use_bands)
    {
      memcpy(ptr, &band_result.band_mask, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      memcpy(ptr, band_result.constant_values.data(), band_result.constant_values.size());
      ptr += band_result.constant_values.size();
      memcpy(ptr, compressed_compacted.data.data(), compressed_compacted.size);
    }
    else
    {
      memcpy(ptr, compressed_full.data.data(), compressed_full.size);
    }

    result.data = std::move(output);
    result.size = total;
  }
}

compression_result_t compressor_huff0_t::compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count)
{
  compression_result_t result;

  bool is_r64 = (format.type == points_type_r64 && format.components == points_components_1);

  if (is_r64 && size >= 8)
  {
    // Path A: offset subtraction only
    std::vector<uint8_t> working_a(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    double min_value = offset_subtract_f64(working_a.data(), size);

    compression_result_t result_a;
    huf_compress_standard(data, size, format, point_count, compression_flag_offset_subtracted, working_a, result_a);
    if (result_a.data && result_a.error.code == 0)
    {
      compression_header_t hdr_a;
      memcpy(&hdr_a, result_a.data.get(), sizeof(hdr_a));
      if (hdr_a.method != compression_method_t::none)
      {
        uint32_t old_payload = hdr_a.compressed_size;
        uint32_t new_payload = 8 + old_payload;
        hdr_a.compressed_size = new_payload;
        hdr_a.flags |= compression_flag_offset_subtracted;
        uint32_t total = static_cast<uint32_t>(sizeof(hdr_a)) + new_payload;
        auto output = std::make_shared<uint8_t[]>(total);
        memcpy(output.get(), &hdr_a, sizeof(hdr_a));
        memcpy(output.get() + sizeof(hdr_a), &min_value, 8);
        memcpy(output.get() + sizeof(hdr_a) + 8, result_a.data.get() + sizeof(hdr_a), old_payload);
        result_a.data = std::move(output);
        result_a.size = total;
      }
    }

    // Also try the baseline
    std::vector<uint8_t> working_base;
    compression_result_t result_base;
    huf_compress_standard(data, size, format, point_count, 0, working_base, result_base);

    result = std::move(result_base);
    if (result_a.data && result_a.size < result.size)
      result = std::move(result_a);
    return result;
  }

  bool is_u16x3 = (format.type == points_type_u16 && format.components == points_components_3);
  if (is_u16x3 && size >= 6)
  {
    // Path A: raw (no preprocessing)
    std::vector<uint8_t> working_a;
    compression_result_t result_a;
    huf_compress_standard(data, size, format, point_count, 0, working_a, result_a);

    // Path B: decorrelate only
    std::vector<uint8_t> working_b(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    decorrelate_u16x3(working_b.data(), size);
    compression_result_t result_b;
    huf_compress_standard(working_b.data(), size, format, point_count, compression_flag_decorrelated, working_b, result_b);

    // Path C: component delta only
    std::vector<uint8_t> working_c(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    delta_encode_u16x3(working_c.data(), size);
    compression_result_t result_c;
    huf_compress_standard(working_c.data(), size, format, point_count, compression_flag_component_delta, working_c, result_c);

    // Path D: decorrelate + component delta
    std::vector<uint8_t> working_d(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    decorrelate_u16x3(working_d.data(), size);
    delta_encode_u16x3(working_d.data(), size);
    compression_result_t result_d;
    huf_compress_standard(working_d.data(), size, format, point_count, compression_flag_decorrelated | compression_flag_component_delta, working_d, result_d);

    // Pick smallest
    result = std::move(result_a);
    if (result_b.data && result_b.size < result.size)
      result = std::move(result_b);
    if (result_c.data && result_c.size < result.size)
      result = std::move(result_c);
    if (result_d.data && result_d.size < result.size)
      result = std::move(result_d);
    return result;
  }

  // Single-component integer path: try element delta
  bool is_morton = (format.type == points_type_m32 || format.type == points_type_m64 || format.type == points_type_m128 || format.type == points_type_m192);
  int elem_size = size_for_format(format.type);
  if (format.components == points_components_1 && !is_morton && !is_r64 && (elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8))
  {
    // Path A: standard (no delta)
    std::vector<uint8_t> working_a;
    compression_result_t result_a;
    huf_compress_standard(data, size, format, point_count, 0, working_a, result_a);

    // Path B: element delta
    std::vector<uint8_t> working_b(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    delta_encode_single(working_b.data(), size, elem_size);
    compression_result_t result_b;
    huf_compress_standard(working_b.data(), size, format, point_count, compression_flag_element_delta, working_b, result_b);

    result = std::move(result_a);
    if (result_b.data && result_b.size < result.size)
      result = std::move(result_b);
    return result;
  }

  std::vector<uint8_t> working;
  huf_compress_standard(data, size, format, point_count, 0, working, result);
  return result;
}

compression_result_t compressor_huff0_t::decompress(const void *data, uint32_t size)
{
  compression_result_t result;

  if (size < sizeof(compression_header_t))
  {
    result.error.code = -1;
    result.error.msg = "Buffer too small for compression header";
    return result;
  }

  compression_header_t header;
  memcpy(&header, data, sizeof(header));

  auto src = static_cast<const uint8_t *>(data) + sizeof(header);

  if (header.method == compression_method_t::none)
  {
    auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);
    memcpy(output.get(), src, header.uncompressed_size);
    result.data = std::move(output);
    result.size = header.uncompressed_size;
    return result;
  }

  // Read offset metadata if present
  double offset_min_value = 0.0;
  if (header.flags & compression_flag_offset_subtracted)
  {
    memcpy(&offset_min_value, src, 8);
    src += 8;
  }

  // Read delta metadata if present
  uint32_t data_offset = 0;
  if (header.flags & compression_flag_delta_encoded)
  {
    memcpy(&data_offset, src, sizeof(uint32_t));
    src += sizeof(uint32_t);
  }

  // Read band metadata if present
  uint32_t band_mask = 0;
  std::vector<uint8_t> constant_values;

  if (header.flags & compression_flag_constant_bands)
  {
    memcpy(&band_mask, src, sizeof(uint32_t));
    src += sizeof(uint32_t);
    uint32_t num_constants = popcount32(band_mask);
    constant_values.assign(src, src + num_constants);
    src += num_constants;
  }

  // Read chunk count
  uint32_t chunk_count;
  memcpy(&chunk_count, src, sizeof(uint32_t));
  auto *chunk_ptr = src + sizeof(uint32_t);

  // Compute compacted size
  uint32_t stride = static_cast<uint32_t>(header.type_size) * static_cast<uint32_t>(header.component_count);
  uint32_t compacted_size;
  if (band_mask != 0 && stride > 0)
  {
    uint32_t element_count = header.uncompressed_size / stride;
    uint32_t remainder = header.uncompressed_size % stride;
    uint32_t non_constant_bands = stride - popcount32(band_mask);
    compacted_size = non_constant_bands * element_count + remainder;
  }
  else
  {
    compacted_size = header.uncompressed_size;
  }

  std::vector<uint8_t> decompressed_compacted(compacted_size);
  uint32_t decompressed_offset = 0;

  alignas(4) uint32_t decompress_workspace[HUF_DECOMPRESS_WORKSPACE_SIZE_U32];

  for (uint32_t c = 0; c < chunk_count; c++)
  {
    uint32_t uncompressed_chunk_size;
    uint32_t compressed_chunk_size;
    memcpy(&uncompressed_chunk_size, chunk_ptr, sizeof(uint32_t));
    memcpy(&compressed_chunk_size, chunk_ptr + sizeof(uint32_t), sizeof(uint32_t));
    chunk_ptr += 2 * sizeof(uint32_t);

    if (compressed_chunk_size == uncompressed_chunk_size)
    {
      memcpy(decompressed_compacted.data() + decompressed_offset, chunk_ptr, uncompressed_chunk_size);
    }
    else if (compressed_chunk_size == 1 && uncompressed_chunk_size > 1)
    {
      memset(decompressed_compacted.data() + decompressed_offset, chunk_ptr[0], uncompressed_chunk_size);
    }
    else
    {
      HUF_DTable dtable[HUF_DTABLE_SIZE(HUF_TABLELOG_MAX)] = {((uint32_t)(HUF_TABLELOG_MAX) * 0x01000001)};
      size_t decomp_size = HUF_decompress4X_hufOnly_wksp(
        dtable,
        decompressed_compacted.data() + decompressed_offset, uncompressed_chunk_size,
        chunk_ptr, compressed_chunk_size,
        decompress_workspace, sizeof(decompress_workspace), 0);

      if (HUF_isError(decomp_size))
      {
        result.error.code = -1;
        result.error.msg = std::string("HUF_decompress4X_hufOnly_wksp failed: ") + HUF_getErrorName(decomp_size);
        return result;
      }
    }

    chunk_ptr += compressed_chunk_size;
    decompressed_offset += uncompressed_chunk_size;
  }

  // Restore constant bands
  std::vector<uint8_t> shuffled(header.uncompressed_size);
  if (band_mask != 0)
  {
    restore_constant_bands(decompressed_compacted.data(), compacted_size, shuffled.data(), header.uncompressed_size, band_mask, constant_values.data(), header.type_size, header.component_count);
  }
  else
  {
    memcpy(shuffled.data(), decompressed_compacted.data(), header.uncompressed_size);
  }

  // Byte unshuffle
  auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);
  byte_unshuffle(shuffled.data(), output.get(), header.uncompressed_size, header.type_size, header.component_count);

  // Reverse element delta (single-component integer delta)
  if (header.flags & compression_flag_element_delta)
    delta_decode_single(output.get(), header.uncompressed_size, header.type_size);

  // Reverse component delta (applied after decorrelation during compression, so undo first)
  if (header.flags & compression_flag_component_delta)
    delta_decode_u16x3(output.get(), header.uncompressed_size);

  // Reverse decorrelation
  if (header.flags & compression_flag_decorrelated)
    correlate_u16x3(output.get(), header.uncompressed_size);

  // Delta decode only the point data portion
  if (header.flags & compression_flag_delta_encoded)
  {
    uint8_t element_stride = static_cast<uint8_t>(stride);
    uint32_t data_bytes = header.uncompressed_size - data_offset;
    delta_decode_morton(output.get() + data_offset, data_bytes, element_stride);
  }

  // Restore offset
  if (header.flags & compression_flag_offset_subtracted)
  {
    offset_restore_f64(output.get(), header.uncompressed_size, offset_min_value);
  }

  result.data = std::move(output);
  result.size = header.uncompressed_size;
  return result;
}

} // namespace points::converter
