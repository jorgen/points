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
#include <points/converter/laszip_file_convert_callbacks.h>
#include <points/error.h>

#include <string>

#include "processor.hpp"

namespace points
{
namespace converter
{

struct converter_t
{
  converter_t(const char *url, uint64_t url_size, enum converter_open_file_semantics_t semantics)
    : processor(std::string(url, url_size), error)
  {
    processor.set_converter_callbacks(laszip_callbacks());
    if (error.code != 0)
    {
      fmt::print(stderr, "Failed to open cache file: {}\n", error.msg);
      exit(1);
    }
    error = processor.upgrade_to_write(semantics == converter_open_file_semantics_t::open_file_semantics_truncate);
    if (error.code != 0)
    {
      fmt::print(stderr, "Failed to make file writable: {}\n", error.msg);
      exit(1);
    }
    processor.set_pre_init_tree_node_limit(100000);
    processor.set_pre_init_tree_config({0.00025, {-10000, -10000, -10000}});
  }
  error_t error;
  tree_config_t tree_config;
  converter_conversion_status_t status;
  processor_t processor;
};

} // namespace converter
} // namespace points
