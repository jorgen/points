/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2021  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

#include <dew/converter/converter.h>
#include <dew/converter/laszip_file_convert_callbacks.h>
#include <dew/error.h>

#include <string>

#include "processor.hpp"

struct dew_converter_t
{
  dew_converter_t(const char *url, uint64_t url_size, enum dew_converter_open_file_semantics_t semantics, const dew::converter::destination_config_t &destination = {})
    // read_only demands the dataset EXISTS: with can_exist a failed existence probe (missing file, but
    // also an unreachable bucket -- wrong region, missing credentials) silently opened an empty
    // converter that reported "no statistics" instead of the actual error.
    : processor(std::string(url, url_size), semantics == dew_open_file_semantics_read_only ? dew::converter::file_existence_requirement_t::exist : dew::converter::file_existence_requirement_t::can_exist, error, destination)
  {
    if (semantics == dew_open_file_semantics_read_only)
      return;
    processor.set_converter_callbacks(dew_laszip_callbacks());
    if (error.code != 0)
      return;
    error = processor.upgrade_to_write(semantics == dew_converter_open_file_semantics_t::dew_open_file_semantics_truncate);
    if (error.code != 0)
      return;
    // node_point_limit (points per node = read/sort chunk size = blob size lever) defaults to 200k in
    // tree_config_t; override per-conversion with dew_converter_set_node_point_limit.
    // A REOPENED dataset already carries its configuration in the deserialized registry (resume);
    // overriding it would corrupt the morton frame every stored coordinate is expressed in.
    if (!processor.configuration_initialized())
      processor.set_pre_init_tree_config({0.00025, {-10000, -10000, -10000}});
  }
  dew_error_t error;
  dew::core::tree_config_t tree_config;
  dew_converter_conversion_status_t status;
  dew::converter::processor_t processor;
};
