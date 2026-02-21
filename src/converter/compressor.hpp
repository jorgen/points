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
#include <memory>

namespace points::converter
{

enum class compression_method_t : uint8_t
{
  none = 0,
  blosc2 = 1
};

struct compression_header_t
{
  uint8_t magic[4];           // {'P','C','M', 1}  — Points CoMpression v1
  compression_method_t method;
  uint8_t type_size;          // element type size (for future decompressors)
  uint8_t component_count;    // components per element
  uint8_t reserved;
  uint32_t uncompressed_size;
  uint32_t compressed_size;   // size of compressed payload (after this header)
};
static_assert(sizeof(compression_header_t) == 16, "compression_header_t must be 16 bytes");

struct compression_result_t
{
  std::shared_ptr<uint8_t[]> data;
  uint32_t size;
  error_t error;
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

std::unique_ptr<compressor_t> create_compressor(compression_method_t method);

} // namespace points::converter
