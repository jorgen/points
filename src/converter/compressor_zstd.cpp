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
#include "compressor_zstd.hpp"
#include "byte_shuffle.hpp"
#include "compression_preprocess.hpp"
#include "input_header.hpp"

#include <zstd.h>

#include <cstring>
#include <vector>

namespace points::converter
{

static constexpr int zstd_compression_level = 9;

compression_method_t compressor_zstd_t::method() const
{
  return compression_method_t::zstd;
}

static void zstd_compress_standard(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count, uint8_t flags_in,
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

  size_t max_compressed = ZSTD_compressBound(size);
  std::vector<uint8_t> compressed_full(max_compressed);
  size_t compressed_full_size = ZSTD_compress(compressed_full.data(), max_compressed, shuffled.data(), size, zstd_compression_level);

  if (ZSTD_isError(compressed_full_size))
  {
    result.error.code = -1;
    result.error.msg = std::string("ZSTD_compress failed: ") + ZSTD_getErrorName(compressed_full_size);
    return;
  }

  auto band_result = detect_constant_bands(shuffled.data(), size, static_cast<uint8_t>(typesize), static_cast<uint8_t>(format.components));

  bool use_bands = false;
  std::vector<uint8_t> compressed_compacted;
  uint32_t band_meta_size = 0;

  if (band_result.band_mask != 0)
  {
    band_meta_size = sizeof(uint32_t) + static_cast<uint32_t>(band_result.constant_values.size());
    uint32_t compacted_size = static_cast<uint32_t>(band_result.compacted_data.size());

    size_t max_compacted_compressed = ZSTD_compressBound(compacted_size);
    compressed_compacted.resize(max_compacted_compressed);
    size_t compressed_compacted_size = ZSTD_compress(compressed_compacted.data(), max_compacted_compressed, band_result.compacted_data.data(), compacted_size, zstd_compression_level);

    if (!ZSTD_isError(compressed_compacted_size))
    {
      uint32_t total_with_bands = delta_meta_size + band_meta_size + static_cast<uint32_t>(compressed_compacted_size);
      uint32_t total_without_bands = delta_meta_size + static_cast<uint32_t>(compressed_full_size);
      if (total_with_bands < total_without_bands)
      {
        use_bands = true;
        compressed_compacted.resize(compressed_compacted_size);
      }
    }
  }

  uint32_t payload_size;
  if (use_bands)
  {
    flags |= compression_flag_constant_bands;
    payload_size = delta_meta_size + band_meta_size + static_cast<uint32_t>(compressed_compacted.size());
  }
  else
  {
    payload_size = delta_meta_size + static_cast<uint32_t>(compressed_full_size);
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
    header.method = compression_method_t::zstd;
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
      memcpy(ptr, compressed_compacted.data(), compressed_compacted.size());
    }
    else
    {
      memcpy(ptr, compressed_full.data(), compressed_full_size);
    }

    result.data = std::move(output);
    result.size = total;
  }
}

compression_result_t compressor_zstd_t::compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count)
{
  compression_result_t result;

  bool is_r64 = (format.type == type_r64 && format.components == components_1);

  if (is_r64 && size >= 8)
  {
    // Path A: offset subtraction only
    std::vector<uint8_t> working_a(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    double min_value = offset_subtract_f64(working_a.data(), size);

    compression_result_t result_a;
    zstd_compress_standard(data, size, format, point_count, compression_flag_offset_subtracted, working_a, result_a);
    // Embed min_value metadata: prepend 8 bytes before existing payload
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

    // Path B: offset + sort + delta
    uint32_t f64_count = size / 8;
    compression_result_t result_b;
    if (f64_count > 1 && f64_count <= 65535)
    {
      std::vector<uint8_t> working_b(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
      double min_value_b = offset_subtract_f64(working_b.data(), size);

      std::vector<uint16_t> perm(f64_count);
      sort_with_permutation_f64(working_b.data(), size, perm.data());

      // Delta encode the sorted u64 data
      delta_encode_morton(working_b.data(), size, 8);

      // Compress the sorted+delta data via standard path (no additional delta in standard)
      zstd_compress_standard(working_b.data(), size, format, 0, compression_flag_offset_subtracted | compression_flag_sort_permutation, working_b, result_b);

      if (result_b.data && result_b.error.code == 0)
      {
        compression_header_t hdr_b;
        memcpy(&hdr_b, result_b.data.get(), sizeof(hdr_b));
        if (hdr_b.method != compression_method_t::none)
        {
          // Compress the permutation
          uint32_t perm_bytes = f64_count * 2;
          size_t perm_bound = ZSTD_compressBound(perm_bytes);
          std::vector<uint8_t> perm_compressed(perm_bound);
          size_t perm_compressed_size = ZSTD_compress(perm_compressed.data(), perm_bound, perm.data(), perm_bytes, zstd_compression_level);

          if (!ZSTD_isError(perm_compressed_size))
          {
            uint32_t old_payload = hdr_b.compressed_size;
            // Layout: [min_value:8] [perm_compressed_size:4] [perm_compressed] [old_payload]
            uint32_t new_payload = 8 + 4 + static_cast<uint32_t>(perm_compressed_size) + old_payload;
            hdr_b.compressed_size = new_payload;
            hdr_b.flags |= compression_flag_offset_subtracted | compression_flag_sort_permutation;
            uint32_t total = static_cast<uint32_t>(sizeof(hdr_b)) + new_payload;
            auto output = std::make_shared<uint8_t[]>(total);
            uint8_t *ptr = output.get();
            memcpy(ptr, &hdr_b, sizeof(hdr_b)); ptr += sizeof(hdr_b);
            memcpy(ptr, &min_value_b, 8); ptr += 8;
            uint32_t pcs = static_cast<uint32_t>(perm_compressed_size);
            memcpy(ptr, &pcs, 4); ptr += 4;
            memcpy(ptr, perm_compressed.data(), perm_compressed_size); ptr += perm_compressed_size;
            memcpy(ptr, result_b.data.get() + sizeof(hdr_b), old_payload);
            result_b.data = std::move(output);
            result_b.size = total;
          }
          else
          {
            result_b.data = nullptr;
          }
        }
      }
    }

    // Also try the baseline (no offset)
    std::vector<uint8_t> working_base;
    compression_result_t result_base;
    zstd_compress_standard(data, size, format, point_count, 0, working_base, result_base);

    // Pick the smallest
    result = std::move(result_base);
    if (result_a.data && result_a.size < result.size)
      result = std::move(result_a);
    if (result_b.data && result_b.size < result.size)
      result = std::move(result_b);
    return result;
  }

  bool is_u16x3 = (format.type == type_u16 && format.components == components_3);
  if (is_u16x3 && size >= 6)
  {
    // Path A: raw (no preprocessing)
    std::vector<uint8_t> working_a;
    compression_result_t result_a;
    zstd_compress_standard(data, size, format, point_count, 0, working_a, result_a);

    // Path B: decorrelate only
    std::vector<uint8_t> working_b(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    decorrelate_u16x3(working_b.data(), size);
    compression_result_t result_b;
    zstd_compress_standard(working_b.data(), size, format, point_count, compression_flag_decorrelated, working_b, result_b);

    // Path C: component delta only
    std::vector<uint8_t> working_c(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    delta_encode_u16x3(working_c.data(), size);
    compression_result_t result_c;
    zstd_compress_standard(working_c.data(), size, format, point_count, compression_flag_component_delta, working_c, result_c);

    // Path D: decorrelate + component delta
    std::vector<uint8_t> working_d(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    decorrelate_u16x3(working_d.data(), size);
    delta_encode_u16x3(working_d.data(), size);
    compression_result_t result_d;
    zstd_compress_standard(working_d.data(), size, format, point_count, compression_flag_decorrelated | compression_flag_component_delta, working_d, result_d);

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
  bool is_morton = (format.type == type_m32 || format.type == type_m64 || format.type == type_m128 || format.type == type_m192);
  int elem_size = size_for_format(format.type);
  if (format.components == components_1 && !is_morton && !is_r64 && (elem_size == 1 || elem_size == 2 || elem_size == 4 || elem_size == 8))
  {
    // Path A: standard (no delta)
    std::vector<uint8_t> working_a;
    compression_result_t result_a;
    zstd_compress_standard(data, size, format, point_count, 0, working_a, result_a);

    // Path B: element delta
    std::vector<uint8_t> working_b(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    delta_encode_single(working_b.data(), size, elem_size);
    compression_result_t result_b;
    zstd_compress_standard(working_b.data(), size, format, point_count, compression_flag_element_delta, working_b, result_b);

    result = std::move(result_a);
    if (result_b.data && result_b.size < result.size)
      result = std::move(result_b);
    return result;
  }

  // Fallthrough: standard compression
  std::vector<uint8_t> working;
  zstd_compress_standard(data, size, format, point_count, 0, working, result);
  return result;
}

compression_result_t compressor_zstd_t::decompress(const void *data, uint32_t size)
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
  uint32_t offset_meta_size = 0;
  if (header.flags & compression_flag_offset_subtracted)
  {
    memcpy(&offset_min_value, src, 8);
    src += 8;
    offset_meta_size = 8;
  }

  // Read permutation if present
  std::vector<uint16_t> permutation;
  uint32_t perm_meta_size = 0;
  if (header.flags & compression_flag_sort_permutation)
  {
    uint32_t perm_compressed_size;
    memcpy(&perm_compressed_size, src, 4);
    src += 4;
    perm_meta_size = 4 + perm_compressed_size;

    uint32_t f64_count = header.uncompressed_size / 8;
    uint32_t perm_bytes = f64_count * 2;
    permutation.resize(f64_count);
    size_t perm_decompressed = ZSTD_decompress(permutation.data(), perm_bytes, src, perm_compressed_size);
    if (ZSTD_isError(perm_decompressed))
    {
      result.error.code = -1;
      result.error.msg = std::string("ZSTD permutation decompress failed: ") + ZSTD_getErrorName(perm_decompressed);
      return result;
    }
    src += perm_compressed_size;
  }

  // Read delta metadata if present
  uint32_t data_offset = 0;
  uint32_t delta_meta_size = 0;
  if (header.flags & compression_flag_delta_encoded)
  {
    memcpy(&data_offset, src, sizeof(uint32_t));
    src += sizeof(uint32_t);
    delta_meta_size = sizeof(uint32_t);
  }

  // Read band metadata if present
  uint32_t band_mask = 0;
  std::vector<uint8_t> constant_values;
  uint32_t band_meta_size = 0;

  if (header.flags & compression_flag_constant_bands)
  {
    memcpy(&band_mask, src, sizeof(uint32_t));
    src += sizeof(uint32_t);
    uint32_t num_constants = popcount32(band_mask);
    constant_values.assign(src, src + num_constants);
    src += num_constants;
    band_meta_size = sizeof(uint32_t) + num_constants;
  }

  uint32_t compressed_payload_size = header.compressed_size - offset_meta_size - perm_meta_size - delta_meta_size - band_meta_size;

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

  // Decompress
  std::vector<uint8_t> decompressed_compacted(compacted_size);
  size_t decompressed_size = ZSTD_decompress(decompressed_compacted.data(), compacted_size, src, compressed_payload_size);

  if (ZSTD_isError(decompressed_size))
  {
    result.error.code = -1;
    result.error.msg = std::string("ZSTD_decompress failed: ") + ZSTD_getErrorName(decompressed_size);
    return result;
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

  // Delta decode
  if (header.flags & compression_flag_delta_encoded)
  {
    if (header.flags & compression_flag_sort_permutation)
    {
      // Sort/permutation path: delta decode the entire buffer (no data_offset prefix)
      delta_decode_morton(output.get(), header.uncompressed_size, 8);
    }
    else
    {
      uint8_t element_stride = static_cast<uint8_t>(stride);
      uint32_t data_bytes = header.uncompressed_size - data_offset;
      delta_decode_morton(output.get() + data_offset, data_bytes, element_stride);
    }
  }

  // Unsort with permutation
  if (header.flags & compression_flag_sort_permutation)
  {
    unsort_with_permutation_f64(output.get(), header.uncompressed_size, permutation.data());
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
