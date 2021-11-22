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
#include <points/converter/error.h>
#include <points/converter/laszip_file_convert_callbacks.h>

#include <string>

#include "processor.hpp"

namespace points
{
namespace converter
{

struct converter_t
{
  converter_t(const char *cache_filename, uint64_t cache_filename_size)
    : cache_filename(cache_filename, cache_filename_size)
    , processor(*this)
    , convert_callbacks(laszip_callbacks())
    , runtime_callbacks{}
  {
    tree_state.scale[0] = 0.001;
    tree_state.scale[1] = 0.001;
    tree_state.scale[2] = 0.001;
    tree_state.offset[0] = -double(uint64_t(1) << 17);
    tree_state.offset[1] = -double(uint64_t(1) << 17);
    tree_state.offset[2] = -double(uint64_t(1) << 17);
  }
  std::string cache_filename;
  tree_global_state_t tree_state;
  processor_t processor;
  converter_file_convert_callbacks_t convert_callbacks;
  converter_runtime_callbacks_t runtime_callbacks;
  converter_conversion_status_t status;
};

} // namespace converter
} // namespace points
