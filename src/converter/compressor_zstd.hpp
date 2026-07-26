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

#include "compressor.hpp"

namespace points::converter
{

class compressor_zstd_t : public compressor_t
{
public:
  compression_method_t method() const override;
  compression_result_t compress(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count) override;
  compression_result_t decompress(const void *data, uint32_t size) override;

  void set_compression_level(int level) { _compression_level = level; }
  int compression_level() const { return _compression_level; }

private:
  void zstd_compress_standard(const void *data, uint32_t size, const point_format_t &format, uint32_t point_count, uint8_t flags_in,
                              std::vector<uint8_t> &working, compression_result_t &result) const;

  int _compression_level = 9;
};

} // namespace points::converter
