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
#include <points/converter/converter.h>

#include "error_p.h"
#include "processor_p.h"

#include <vector>
#include <string>
#include <memory>

namespace points
{
namespace converter
{

struct header_t
{
  uint64_t point_count = 0;
  uint64_t data_start = 0;
  double offset[3] = {};
  double scale[3] = {};
  std::vector<attribute_t> attributes;
  std::vector<std::unique_ptr<char[]>> attribute_names;
};

void header_set_point_count(header_t *header, uint64_t count)
{
  header->point_count = count;
}

void header_set_data_start(header_t *header, uint64_t offset)
{
  header->data_start = offset;
}

void header_set_coordinate_offset(header_t *header, double offset[3])
{
  memcpy(header->offset, offset, sizeof(header->offset));
}

void header_set_coordinate_scale(header_t *header, double scale[3])
{
  memcpy(header->scale, scale, sizeof(header->scale));
}

void header_add_attribute(header_t *header, const char *name, uint64_t name_size, format_t format, components_t components)
{
  header->attribute_names.emplace_back(new char[name_size + 1]);
  memcpy(header->attribute_names.back().get(), name, name_size);
  header->attribute_names.back().get()[name_size] = 0;
  header->attributes.push_back({header->attribute_names.back().get(), name_size, format, components});
}

void converter_error_get_info(const error_t *error, int *code, const char **str, int *str_len)
{
  *code = error->code;
  *str = error->msg.c_str();
  *str_len = int(error->msg.size());
}

struct converter_t
{
  converter_t(const char *cache_filename, uint64_t cache_filename_size)
    : cache_filename(cache_filename, cache_filename_size)
    , convert_callbacks{}
    , runtime_callbacks{}
  {
  }
  std::string cache_filename;
  processor_t processor;
  converter_convert_callbacks_t convert_callbacks;
  converter_runtime_callbacks_t runtime_callbacks;

  error_t error;
};

struct converter_t *converter_create(const char *cache_filename, uint64_t cache_filename_size)
{
  return new converter_t(cache_filename, cache_filename_size);
}

void converter_destroy(converter_t *destroy)
{
  delete destroy;
}

void converter_add_converter_callbacks(converter_t *converter, converter_convert_callbacks_t callbacks)
{
  converter->convert_callbacks = callbacks;
}

void converter_add_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks)
{
  converter->runtime_callbacks = callbacks;
}

void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count)
{
  std::vector<std::string> input_files;
  for (uint32_t i = 0; i < buffer_count; i++)
  {
    input_files.push_back(std::string(buffers[i].data, buffers[i].size)); 
  }
  converter->processor.add_files(input_files);
}

void converter_add_data(converter_t *converter, const char *data_name, uint64_t data_name_size, const void *data, uint64_t data_size)
{
  (void)converter;
  (void)data_name;
  (void)data_name_size;
  (void)data;
  (void)data_size;
}

void converter_wait_finish(converter_t *converter)
{
  (void)converter;
}

const error_t *converter_get_current_error(const converter_t *converter)
{
  if (converter->error.code)
    return &converter->error;
  return nullptr;
}


} // namespace converter
} // namespace points
