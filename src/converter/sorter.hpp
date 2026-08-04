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

#include "attributes_configs.hpp"
#include "dataset_types.hpp"
#include "converter.hpp"


namespace dew::converter
{
void sort_points(const tree_config_t &tree_config, attributes_configs_t &attributes_configs, const dew_converter_header_t &public_header, points_t &points, dew_error_t &error, bool store_original_order = false);
}

