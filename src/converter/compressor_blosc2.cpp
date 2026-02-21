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
#include "compressor_blosc2.hpp"
#include "input_header.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <blosc2.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <cstring>

namespace points::converter
{

compressor_blosc2_t::compressor_blosc2_t()
{
  blosc2_init();
}

compressor_blosc2_t::~compressor_blosc2_t()
{
  blosc2_destroy();
}

compression_method_t compressor_blosc2_t::method() const
{
  return compression_method_t::blosc2;
}

compression_result_t compressor_blosc2_t::compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count)
{
  (void)point_count;
  compression_result_t result;

  int typesize = size_for_format(format.type) * static_cast<int>(format.components);
  if (typesize <= 0)
    typesize = 1;

  int32_t max_compressed = static_cast<int32_t>(size) + BLOSC2_MAX_OVERHEAD;
  uint32_t total_output_max = static_cast<uint32_t>(sizeof(compression_header_t)) + static_cast<uint32_t>(max_compressed);
  auto output = std::make_shared<uint8_t[]>(total_output_max);

  blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
  cparams.compcode = BLOSC_ZSTD;
  cparams.clevel = 5;
  cparams.typesize = typesize;
  cparams.nthreads = 1;

  blosc2_context *cctx = blosc2_create_cctx(cparams);
  if (!cctx)
  {
    result.error.code = -1;
    result.error.msg = "Failed to create blosc2 compression context";
    return result;
  }

  int compressed_size = blosc2_compress_ctx(cctx, data, static_cast<int32_t>(size),
                                            output.get() + sizeof(compression_header_t),
                                            max_compressed);
  blosc2_free_ctx(cctx);

  if (compressed_size < 0)
  {
    result.error.code = compressed_size;
    result.error.msg = "blosc2_compress_ctx failed";
    return result;
  }

  compression_header_t header;
  header.magic[0] = 'P';
  header.magic[1] = 'C';
  header.magic[2] = 'M';
  header.magic[3] = 1;
  header.type_size = static_cast<uint8_t>(size_for_format(format.type));
  header.component_count = static_cast<uint8_t>(format.components);
  header.reserved = 0;
  header.uncompressed_size = size;

  if (static_cast<uint32_t>(compressed_size) >= size)
  {
    header.method = compression_method_t::none;
    header.compressed_size = size;
    memcpy(output.get(), &header, sizeof(header));
    memcpy(output.get() + sizeof(header), data, size);
    result.size = static_cast<uint32_t>(sizeof(header)) + size;
  }
  else
  {
    header.method = compression_method_t::blosc2;
    header.compressed_size = static_cast<uint32_t>(compressed_size);
    memcpy(output.get(), &header, sizeof(header));
    result.size = static_cast<uint32_t>(sizeof(header)) + static_cast<uint32_t>(compressed_size);
  }

  result.data = std::move(output);
  return result;
}

compression_result_t compressor_blosc2_t::decompress(const void *data, uint32_t size)
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

  auto src = static_cast<const uint8_t *>(data) + sizeof(header);

  if (header.method == compression_method_t::none)
  {
    auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);
    memcpy(output.get(), src, header.uncompressed_size);
    result.data = std::move(output);
    result.size = header.uncompressed_size;
    return result;
  }

  auto output = std::make_shared<uint8_t[]>(header.uncompressed_size);

  blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
  dparams.nthreads = 1;

  blosc2_context *dctx = blosc2_create_dctx(dparams);
  if (!dctx)
  {
    result.error.code = -1;
    result.error.msg = "Failed to create blosc2 decompression context";
    return result;
  }

  int decompressed_size = blosc2_decompress_ctx(dctx, src, static_cast<int32_t>(header.compressed_size),
                                                output.get(), static_cast<int32_t>(header.uncompressed_size));
  blosc2_free_ctx(dctx);

  if (decompressed_size < 0)
  {
    result.error.code = decompressed_size;
    result.error.msg = "blosc2_decompress_ctx failed";
    return result;
  }

  result.data = std::move(output);
  result.size = header.uncompressed_size;
  return result;
}

std::unique_ptr<compressor_t> create_compressor(compression_method_t method)
{
  switch (method)
  {
  case compression_method_t::blosc2:
    return std::make_unique<compressor_blosc2_t>();
  case compression_method_t::none:
    return nullptr;
  }
  return nullptr;
}

} // namespace points::converter
