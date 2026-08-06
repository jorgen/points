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

#include <stdint.h>

#include <vector>
#include <memory>

#include <dew/converter/converter.h>

#include "dataset_types.hpp"

#include <format_util.hpp>  // size_for_format (moved to the core)


namespace dew::converter
{
using namespace dew::core;
void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count);
void attribute_buffers_initialize(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count, std::unique_ptr<uint8_t[]> && morton_attribute_buffer);
void attribute_buffers_adjust_buffers_to_size(const std::vector<point_format_t> &attributes_def, attribute_buffers_t &buffers, uint32_t point_count);
}


