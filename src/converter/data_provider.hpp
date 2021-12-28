/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
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

#include <points/converter/converter.h>

#include "conversion_types.hpp"
#include "morton.hpp"
#include "cache_file_handler.hpp"

namespace points
{
namespace converter
{
class cache_file_handler_t;
class points_data_provider_t;
struct points_data_t
{
  points_data_t(cache_file_handler_t &cache_file, input_data_id_t &input_id, format_t format, uint8_t *points, uint64_t size)
    : cache_file(cache_file)
    , input_id(input_id)
    , format(format)
    , points(points)
    , size(size)
  {}
  ~points_data_t()
  {
    cache_file.deref(input_id);
  }
  points_data_t(const points_data_t &) = delete;
  points_data_t(points_data_t &&) = delete;
  points_data_t &operator=(const points_data_t &) = delete;
  cache_file_handler_t &cache_file;
  input_data_id_t input_id;
  format_t format;
  uint8_t *points;
  uint64_t size;
};

class points_data_provider_t
{
public:
  points_data_provider_t(cache_file_handler_t &cache_file_handler);

  points_data_t read_input_id(input_data_id_t id);

private:
  cache_file_handler_t &_cache_file_handler;
};
}
}
