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
#ifndef DEW_CONVERTER_TYPES_H
#define DEW_CONVERTER_TYPES_H

// The attribute/buffer value types shared by the conversion API and the dataset read path.
//
// These live in their own header because they are not converter-specific: the storage map, the
// attribute registry and every decode path describe data with them, so they are the natural
// foundation of a dataset-access module. Splitting them out of converter.h first (with no rename
// and no call-site change) keeps the later move to <dew/access/types.h> a mechanical one.

#include <stdint.h>

#include <dew/core/format.h>
#include <dew/converter/export.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque, append-only attribute set. Built by the file-convert `init` callback and consumed by the
// attribute registry; the C++ definition lives with the internal dataset types.
struct dew_converter_attributes_t;
DEW_CONVERTER_EXPORT void dew_converter_attributes_add_attribute(struct dew_converter_attributes_t *attributes, const char *name, uint32_t name_size, enum dew_type_t format, enum dew_components_t components);

//= py.skip
struct dew_converter_attribute_t
{
#ifdef __cplusplus
  dew_converter_attribute_t(const char *a_name, uint32_t a_name_size, enum dew_type_t format, enum dew_components_t a_components)
    : name(a_name)
    , name_size(a_name_size)
    , type(format)
    , components(a_components)
  {
  }
#endif

  const char *name;
  uint32_t name_size;
  enum dew_type_t type;
  enum dew_components_t components;
};

//= py.skip
struct dew_converter_buffer_t
{
#ifdef __cplusplus
  dew_converter_buffer_t()
    : data(nullptr)
    , size(0)
  {
  }

  dew_converter_buffer_t(void *a_data, uint32_t a_size)
    : data(a_data)
    , size(a_size)
  {
  }
#endif

  void *data;
  uint32_t size;
};

#ifdef __cplusplus
}
#endif

#endif // DEW_CONVERTER_TYPES_H
