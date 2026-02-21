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

compression_method_t compressor_zstd_t::method() const
{
  return compression_method_t::zstd;
}

compression_result_t compressor_zstd_t::compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count)
{
  compression_result_t result;

  int typesize = size_for_format(format.type);
  if (typesize <= 0)
    typesize = 1;

  uint8_t element_stride = static_cast<uint8_t>(typesize * static_cast<int>(format.components));
  uint8_t flags = 0;

  // Compute data offset — buffer 0 has a storage_header_t prepended before the point data
  uint32_t data_bytes = static_cast<uint32_t>(point_count) * static_cast<uint32_t>(element_stride);
  uint32_t data_offset = (data_bytes > 0 && data_bytes <= size) ? (size - data_bytes) : 0;

  // Delta encode morton codes (in-place on a copy), skipping the prefix
  std::vector<uint8_t> working(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
  if (data_bytes > 0 && delta_encode_morton(working.data() + data_offset, data_bytes, element_stride))
    flags |= compression_flag_delta_encoded;

  // Byte shuffle the entire buffer
  std::vector<uint8_t> shuffled(size);
  byte_shuffle(working.data(), shuffled.data(), size, static_cast<uint32_t>(typesize), static_cast<uint32_t>(format.components));

  uint32_t delta_meta_size = (flags & compression_flag_delta_encoded) ? sizeof(uint32_t) : 0;

  // Compress full shuffled data (baseline)
  size_t max_compressed = ZSTD_compressBound(size);
  std::vector<uint8_t> compressed_full(max_compressed);
  size_t compressed_full_size = ZSTD_compress(compressed_full.data(), max_compressed, shuffled.data(), size, 3);

  if (ZSTD_isError(compressed_full_size))
  {
    result.error.code = -1;
    result.error.msg = std::string("ZSTD_compress failed: ") + ZSTD_getErrorName(compressed_full_size);
    return result;
  }

  // Try constant band removal — only use if it beats the baseline
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
    size_t compressed_compacted_size = ZSTD_compress(compressed_compacted.data(), max_compacted_compressed, band_result.compacted_data.data(), compacted_size, 3);

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

  // Pick the winning payload
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

  uint32_t compressed_payload_size = header.compressed_size - delta_meta_size - band_meta_size;

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

  // Delta decode only the point data portion
  if (header.flags & compression_flag_delta_encoded)
  {
    uint8_t element_stride = static_cast<uint8_t>(stride);
    uint32_t data_bytes = header.uncompressed_size - data_offset;
    delta_decode_morton(output.get() + data_offset, data_bytes, element_stride);
  }

  result.data = std::move(output);
  result.size = header.uncompressed_size;
  return result;
}

} // namespace points::converter
