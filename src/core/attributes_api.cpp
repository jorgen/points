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

// The one exported C entry point that fills a dew_attributes_t. It lives with the type rather than
// with the converter's input handling, so that describing a dataset's attributes does not require
// linking the write pipeline.

#include <dew/core/types.h>

#include "dataset_types.hpp"

#include <cstring>

void dew_attributes_add_attribute(struct dew_attributes_t *attributes, const char *name, uint32_t name_size, enum dew_type_t format, enum dew_components_t components)
{
  attributes->attribute_names.emplace_back(new char[name_size + 1]);
  memcpy(attributes->attribute_names.back().get(), name, name_size);
  attributes->attribute_names.back().get()[name_size] = 0;
  attributes->attributes.push_back({attributes->attribute_names.back().get(), name_size, format, components});
}
