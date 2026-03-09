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
#pragma once

#include "conversion_types.hpp"
#include "error.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace points::converter
{

enum class compression_method_t : uint8_t
{
  none = 0,
  blosc2 = 1,
  zstd = 2,
  huff0 = 3,
  constant = 4,
  ans = 5
};

static constexpr uint8_t compression_flag_delta_encoded      = 1 << 0;
static constexpr uint8_t compression_flag_constant_bands     = 1 << 1;
static constexpr uint8_t compression_flag_offset_subtracted  = 1 << 2;
static constexpr uint8_t compression_flag_sort_permutation   = 1 << 3;
static constexpr uint8_t compression_flag_decorrelated       = 1 << 4;
static constexpr uint8_t compression_flag_component_delta    = 1 << 5;
static constexpr uint8_t compression_flag_element_delta      = 1 << 6;

struct compression_header_t
{
  uint8_t magic[4];           // {'P','C','M', 1}  — Points CoMpression v1
  compression_method_t method;
  uint8_t type_size;          // element type size (for future decompressors)
  uint8_t component_count;    // components per element
  uint8_t flags;              // preprocessing flags (was reserved, 0 = backward compat)
  uint32_t uncompressed_size;
  uint32_t compressed_size;   // size of compressed payload (after this header)
};
static_assert(sizeof(compression_header_t) == 16, "compression_header_t must be 16 bytes");

struct compression_result_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size = 0;
  points_error_t error;
};

class compressor_t
{
public:
  virtual ~compressor_t() = default;
  virtual compression_method_t method() const = 0;
  virtual compression_result_t compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count) = 0;
  virtual compression_result_t decompress(const void *data, uint32_t size) = 0;
};

inline bool has_compression_magic(const void *data, uint32_t size)
{
  if (size < sizeof(compression_header_t))
    return false;
  auto bytes = static_cast<const uint8_t *>(data);
  return bytes[0] == 'P' && bytes[1] == 'C' && bytes[2] == 'M' && bytes[3] == 1;
}

struct attribute_compression_stats_t
{
  std::string name;
  point_format_t format;
  uint64_t buffer_count = 0;
  uint64_t uncompressed_bytes = 0;
  uint64_t compressed_bytes = 0;
  double min_value = std::numeric_limits<double>::max();
  double max_value = std::numeric_limits<double>::lowest();
  uint64_t path_counts[4] = {}; // [0]=raw, [1]=decorrelated, [2]=component_delta, [3]=decorrelated+component_delta
  uint64_t lod_buffer_count = 0;
  uint64_t lod_uncompressed_bytes = 0;
  uint64_t lod_compressed_bytes = 0;
};

struct compression_stats_t
{
  uint32_t input_file_count = 0;
  uint32_t total_buffer_count = 0;
  uint32_t lod_buffer_count = 0;
  compression_method_t method = compression_method_t::none;
  uint64_t input_file_size_bytes = 0;
  std::vector<attribute_compression_stats_t> per_attribute;

  void accumulate(const std::string &name, const point_format_t &format, uint32_t uncompressed, uint32_t compressed, double min_val = std::numeric_limits<double>::max(), double max_val = std::numeric_limits<double>::lowest(), uint8_t flags = 0, bool is_lod = false);
  std::shared_ptr<uint8_t[]> serialize(uint32_t &out_size) const;
  static compression_stats_t deserialize(const uint8_t *data, uint32_t size);
};

std::unique_ptr<compressor_t> create_compressor(compression_method_t method);
compression_result_t try_compress_constant(const void *data, uint32_t size, const point_format_t &format);
compression_result_t decompress_any(const void *data, uint32_t size);

} // namespace points::converter
