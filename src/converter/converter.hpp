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

struct points_converter_t
{
  points_converter_t(const char *url, uint64_t url_size, enum points_converter_open_file_semantics_t semantics)
    : processor(std::string(url, url_size), points::converter::file_existence_requirement_t::can_exist, error)
  {
    if (semantics == points_open_file_semantics_read_only)
      return;
    processor.set_converter_callbacks(points_laszip_callbacks());
    if (error.code != 0)
      return;
    error = processor.upgrade_to_write(semantics == points_converter_open_file_semantics_t::points_open_file_semantics_truncate);
    if (error.code != 0)
      return;
    processor.set_pre_init_tree_node_limit(100000);
    processor.set_pre_init_tree_config({0.00025, {-10000, -10000, -10000}});
  }
  points_error_t error;
  points::converter::tree_config_t tree_config;
  points_converter_conversion_status_t status;
  points::converter::processor_t processor;
};
