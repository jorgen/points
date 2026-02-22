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
#include "compressor.hpp"
#include "compressor_blosc2.hpp"
#include "compressor_zstd.hpp"
#include "compressor_fse.hpp"
#include "input_header.hpp"

#include <cstring>

namespace points::converter
{

std::unique_ptr<compressor_t> create_compressor(compression_method_t method)
{
  switch (method)
  {
  case compression_method_t::blosc2:
    return std::make_unique<compressor_blosc2_t>();
  case compression_method_t::zstd:
    return std::make_unique<compressor_zstd_t>();
  case compression_method_t::huff0:
    return std::make_unique<compressor_huff0_t>();
  case compression_method_t::none:
  case compression_method_t::constant:
    return nullptr;
  }
  return nullptr;
}

compression_result_t try_compress_constant(const void *data, uint32_t size, const point_format_t &format)
{
  compression_result_t result;

  int element_size = size_for_format(format.type) * static_cast<int>(format.components);
  if (element_size <= 0 || static_cast<uint32_t>(element_size) > size)
    return result;

  auto bytes = static_cast<const uint8_t *>(data);
  const uint8_t *first_element = bytes;

  for (uint32_t offset = static_cast<uint32_t>(element_size); offset + static_cast<uint32_t>(element_size) <= size; offset += static_cast<uint32_t>(element_size))
  {
    if (memcmp(first_element, bytes + offset, static_cast<size_t>(element_size)) != 0)
      return result; // not constant — return empty result
  }

  // Buffer is constant — store header + single element
  uint32_t total = static_cast<uint32_t>(sizeof(compression_header_t)) + static_cast<uint32_t>(element_size);
  auto output = std::make_shared<uint8_t[]>(total);

  compression_header_t header;
  header.magic[0] = 'P';
  header.magic[1] = 'C';
  header.magic[2] = 'M';
  header.magic[3] = 1;
  header.method = compression_method_t::constant;
  header.type_size = static_cast<uint8_t>(size_for_format(format.type));
  header.component_count = static_cast<uint8_t>(format.components);
  header.flags = 0;
  header.uncompressed_size = size;
  header.compressed_size = static_cast<uint32_t>(element_size);

  memcpy(output.get(), &header, sizeof(header));
  memcpy(output.get() + sizeof(header), first_element, static_cast<size_t>(element_size));

  result.data = std::move(output);
  result.size = total;
  return result;
}

compression_result_t decompress_any(const void *data, uint32_t size)
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

  if (header.magic[0] != 'P' || header.magic[1] != 'C' || header.magic[2] != 'M' || header.magic[3] != 1)
  {
    result.error.code = -1;
    result.error.msg = "Invalid compression magic";
    return result;
  }

  switch (header.method)
  {
  case compression_method_t::none:
  {
    auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);
    memcpy(output.get(), static_cast<const uint8_t *>(data) + sizeof(header), header.uncompressed_size);
    result.data = std::move(output);
    result.size = header.uncompressed_size;
    return result;
  }
  case compression_method_t::blosc2:
  {
    compressor_blosc2_t decompressor;
    return decompressor.decompress(data, size);
  }
  case compression_method_t::zstd:
  {
    compressor_zstd_t decompressor;
    return decompressor.decompress(data, size);
  }
  case compression_method_t::huff0:
  {
    compressor_huff0_t decompressor;
    return decompressor.decompress(data, size);
  }
  case compression_method_t::constant:
  {
    uint32_t element_size = static_cast<uint32_t>(header.type_size) * static_cast<uint32_t>(header.component_count);
    if (element_size == 0 || header.compressed_size != element_size)
    {
      result.error.code = -1;
      result.error.msg = "Invalid constant compression payload";
      return result;
    }
    auto src = static_cast<const uint8_t *>(data) + sizeof(header);
    auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);
    auto *dst = output.get();
    for (uint32_t offset = 0; offset + element_size <= header.uncompressed_size; offset += element_size)
    {
      memcpy(dst + offset, src, element_size);
    }
    result.data = std::move(output);
    result.size = header.uncompressed_size;
    return result;
  }
  }

  result.error.code = -1;
  result.error.msg = "Unknown compression method";
  return result;
}

void compression_stats_t::accumulate(const std::string &name, const point_format_t &format, uint32_t uncompressed, uint32_t compressed, double min_val, double max_val, uint8_t flags, bool is_lod)
{
  total_buffer_count++;
  if (is_lod)
    lod_buffer_count++;

  uint32_t path_idx = 0;
  if (flags & compression_flag_decorrelated)
    path_idx |= 1;
  if (flags & compression_flag_component_delta)
    path_idx |= 2;

  for (auto &attr : per_attribute)
  {
    if (attr.name == name && attr.format.type == format.type && attr.format.components == format.components)
    {
      attr.buffer_count++;
      attr.uncompressed_bytes += uncompressed;
      attr.compressed_bytes += compressed;
      if (min_val < attr.min_value)
        attr.min_value = min_val;
      if (max_val > attr.max_value)
        attr.max_value = max_val;
      attr.path_counts[path_idx]++;
      if (is_lod)
      {
        attr.lod_buffer_count++;
        attr.lod_uncompressed_bytes += uncompressed;
        attr.lod_compressed_bytes += compressed;
      }
      return;
    }
  }
  auto &attr = per_attribute.emplace_back();
  attr.name = name;
  attr.format = format;
  attr.buffer_count = 1;
  attr.uncompressed_bytes = uncompressed;
  attr.compressed_bytes = compressed;
  attr.min_value = min_val;
  attr.max_value = max_val;
  attr.path_counts[path_idx] = 1;
  if (is_lod)
  {
    attr.lod_buffer_count = 1;
    attr.lod_uncompressed_bytes = uncompressed;
    attr.lod_compressed_bytes = compressed;
  }
}

std::shared_ptr<uint8_t[]> compression_stats_t::serialize(uint32_t &out_size) const
{
  // compute total size
  uint32_t size = 4 + 4 + 4 + 4 + 1 + 3 + 4; // version, input_file_count, total_buffer_count, lod_buffer_count, method, padding, attribute_count
  for (auto &attr : per_attribute)
  {
    size += 4;                                  // name_size
    size += static_cast<uint32_t>(attr.name.size()); // name
    size += 1 + 1 + 2;                          // type, components, padding
    size += 8 + 8 + 8;                          // buffer_count, uncompressed, compressed
    size += 8 + 8;                              // min_value, max_value
    size += 4 * 8;                              // path_counts[4]
    size += 8 + 8 + 8;                          // lod_buffer_count, lod_uncompressed, lod_compressed
  }

  auto data = std::make_shared<uint8_t[]>(size);
  auto *ptr = data.get();
  memset(ptr, 0, size);

  uint32_t version = 4;
  memcpy(ptr, &version, 4); ptr += 4;
  memcpy(ptr, &input_file_count, 4); ptr += 4;
  memcpy(ptr, &total_buffer_count, 4); ptr += 4;
  memcpy(ptr, &lod_buffer_count, 4); ptr += 4;
  auto m = static_cast<uint8_t>(method);
  memcpy(ptr, &m, 1); ptr += 1;
  ptr += 3; // padding
  uint32_t attr_count = static_cast<uint32_t>(per_attribute.size());
  memcpy(ptr, &attr_count, 4); ptr += 4;

  for (auto &attr : per_attribute)
  {
    uint32_t name_size = static_cast<uint32_t>(attr.name.size());
    memcpy(ptr, &name_size, 4); ptr += 4;
    memcpy(ptr, attr.name.data(), name_size); ptr += name_size;
    auto type = static_cast<uint8_t>(attr.format.type);
    auto comp = static_cast<uint8_t>(attr.format.components);
    memcpy(ptr, &type, 1); ptr += 1;
    memcpy(ptr, &comp, 1); ptr += 1;
    ptr += 2; // padding
    memcpy(ptr, &attr.buffer_count, 8); ptr += 8;
    memcpy(ptr, &attr.uncompressed_bytes, 8); ptr += 8;
    memcpy(ptr, &attr.compressed_bytes, 8); ptr += 8;
    memcpy(ptr, &attr.min_value, 8); ptr += 8;
    memcpy(ptr, &attr.max_value, 8); ptr += 8;
    memcpy(ptr, attr.path_counts, 4 * 8); ptr += 4 * 8;
    memcpy(ptr, &attr.lod_buffer_count, 8); ptr += 8;
    memcpy(ptr, &attr.lod_uncompressed_bytes, 8); ptr += 8;
    memcpy(ptr, &attr.lod_compressed_bytes, 8); ptr += 8;
  }

  out_size = size;
  return data;
}

compression_stats_t compression_stats_t::deserialize(const uint8_t *data, uint32_t size)
{
  compression_stats_t stats;
  if (size < 20) // minimum header size
    return stats;

  auto *ptr = data;
  uint32_t version;
  memcpy(&version, ptr, 4); ptr += 4;
  if (version < 1 || version > 4)
    return stats;

  memcpy(&stats.input_file_count, ptr, 4); ptr += 4;
  memcpy(&stats.total_buffer_count, ptr, 4); ptr += 4;
  if (version >= 4)
  {
    memcpy(&stats.lod_buffer_count, ptr, 4); ptr += 4;
  }
  uint8_t m;
  memcpy(&m, ptr, 1); ptr += 1;
  stats.method = static_cast<compression_method_t>(m);
  ptr += 3; // padding

  uint32_t attr_count;
  memcpy(&attr_count, ptr, 4); ptr += 4;
  uint32_t per_attr_fixed_size = 28; // v1 base
  if (version >= 2)
    per_attr_fixed_size += 16; // min/max
  if (version >= 3)
    per_attr_fixed_size += 32; // path_counts[4]
  if (version >= 4)
    per_attr_fixed_size += 24; // lod_buffer_count, lod_uncompressed, lod_compressed

  auto remaining = static_cast<uint32_t>(size - static_cast<uint32_t>(ptr - data));
  for (uint32_t i = 0; i < attr_count && remaining >= 4; i++)
  {
    uint32_t name_size;
    memcpy(&name_size, ptr, 4); ptr += 4;
    remaining -= 4;

    if (name_size > remaining)
      break;

    auto &attr = stats.per_attribute.emplace_back();
    attr.name.assign(reinterpret_cast<const char *>(ptr), name_size);
    ptr += name_size;
    remaining -= name_size;

    if (remaining < per_attr_fixed_size)
      break;

    uint8_t type_val, comp_val;
    memcpy(&type_val, ptr, 1); ptr += 1;
    memcpy(&comp_val, ptr, 1); ptr += 1;
    ptr += 2; // padding
    attr.format.type = static_cast<type_t>(type_val);
    attr.format.components = static_cast<components_t>(comp_val);

    memcpy(&attr.buffer_count, ptr, 8); ptr += 8;
    memcpy(&attr.uncompressed_bytes, ptr, 8); ptr += 8;
    memcpy(&attr.compressed_bytes, ptr, 8); ptr += 8;
    remaining -= 28;
    if (version >= 2)
    {
      memcpy(&attr.min_value, ptr, 8); ptr += 8;
      memcpy(&attr.max_value, ptr, 8); ptr += 8;
      remaining -= 16;
    }
    if (version >= 3)
    {
      memcpy(attr.path_counts, ptr, 4 * 8); ptr += 4 * 8;
      remaining -= 32;
    }
    if (version >= 4)
    {
      memcpy(&attr.lod_buffer_count, ptr, 8); ptr += 8;
      memcpy(&attr.lod_uncompressed_bytes, ptr, 8); ptr += 8;
      memcpy(&attr.lod_compressed_bytes, ptr, 8); ptr += 8;
      remaining -= 24;
    }
  }

  return stats;
}

} // namespace points::converter
