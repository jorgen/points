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
#include "converter_p.h"

#include "error_p.h"
#include "processor_p.h"

#include <vector>
#include <string>
#include <memory>

namespace points
{
namespace converter
{
error_t *converter_error_create()
{
  return new error_t();
}

void converter_error_destroy(error_t *error)
{
  delete error;
}

void converter_error_set_info(error_t *error, int code, const char *str, size_t str_len)
{
  error->code = code;
  error->msg = std::string(str, str_len);
}
void converter_error_get_info(const error_t *error, int *code, const char **str, size_t *str_len)
{
  *code = error->code;
  *str = error->msg.c_str();
  *str_len = error->msg.size();
}

struct converter_t *converter_create(const char *cache_filename, uint64_t cache_filename_size)
{
  return new converter_t(cache_filename, cache_filename_size);
}

void converter_destroy(converter_t *destroy)
{
  delete destroy;
}

void converter_add_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks)
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

void converter_wait_finish(converter_t *converter)
{
  (void)converter;
}

converter_conversion_status_t converter_status(converter_t *converter)
{
  return converter->status;
}


} // namespace converter
} // namespace points
