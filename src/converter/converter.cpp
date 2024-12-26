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
#include "converter.hpp"
#include <points/converter/converter.h>

#include "processor.hpp"

#include <memory>
#include <vector>


namespace points::converter
{
struct converter_t *converter_create(const char *url, uint64_t url_size, enum converter_open_file_semantics_t semantics)
{
  return new converter_t(url, url_size, semantics);
}

void converter_destroy(converter_t *destroy)
{
  delete destroy;
}

void converter_set_file_converter_callbacks(converter_t *converter, converter_file_convert_callbacks_t callbacks)
{
  converter->processor.set_converter_callbacks(callbacks);
}

void converter_set_runtime_callbacks(converter_t *converter, converter_runtime_callbacks_t callbacks, void *user_ptr)
{
  converter->processor.set_runtime_callbacks(callbacks, user_ptr);
}

void converter_add_data_file(converter_t *converter, str_buffer *buffers, uint32_t buffer_count)
{
  std::vector<std::pair<std::unique_ptr<char[]>, uint32_t>> input_files;
  input_files.reserve(buffer_count);
  for (uint32_t i = 0; i < buffer_count; i++)
  {
    input_files.emplace_back();
    auto &input_data_source = input_files.back();
    input_data_source.first.reset(new char[buffers[i].size + 1]);
    memcpy(input_data_source.first.get(), buffers[i].data, buffers[i].size);
    input_data_source.first.get()[buffers[i].size] = 0;
    input_data_source.second = buffers[i].size;
  }
  converter->processor.add_files(std::move(input_files));
}

void converter_wait_idle(converter_t *converter)
{
  converter->processor.wait_idle();
}

converter_conversion_status_t converter_status(converter_t *converter)
{
  return converter->status;
}

} // namespace points::converter

