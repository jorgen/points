/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
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
#include "byte_shuffle.hpp"

#include <cstring>

namespace points::converter
{

// Layout: for each component c, for each byte b within the type,
// one band of element_count bytes: all point[i]'s component c byte b.
// Band index = c * typesize + b, band offset = band_index * element_count.

void byte_shuffle(const uint8_t *src, uint8_t *dst, uint32_t total_size, uint32_t typesize, uint32_t component_count)
{
  uint32_t stride = typesize * component_count;
  if (stride <= 1 || total_size == 0)
  {
    memcpy(dst, src, total_size);
    return;
  }
  uint32_t element_count = total_size / stride;
  uint32_t remainder = total_size % stride;
  for (uint32_t i = 0; i < element_count; i++)
  {
    const uint8_t *point = src + i * stride;
    for (uint32_t c = 0; c < component_count; c++)
    {
      for (uint32_t b = 0; b < typesize; b++)
      {
        uint32_t band = (c * typesize + b) * element_count;
        dst[band + i] = point[c * typesize + b];
      }
    }
  }
  if (remainder > 0)
  {
    memcpy(dst + stride * element_count, src + stride * element_count, remainder);
  }
}

void byte_unshuffle(const uint8_t *src, uint8_t *dst, uint32_t total_size, uint32_t typesize, uint32_t component_count)
{
  uint32_t stride = typesize * component_count;
  if (stride <= 1 || total_size == 0)
  {
    memcpy(dst, src, total_size);
    return;
  }
  uint32_t element_count = total_size / stride;
  uint32_t remainder = total_size % stride;
  for (uint32_t i = 0; i < element_count; i++)
  {
    uint8_t *point = dst + i * stride;
    for (uint32_t c = 0; c < component_count; c++)
    {
      for (uint32_t b = 0; b < typesize; b++)
      {
        uint32_t band = (c * typesize + b) * element_count;
        point[c * typesize + b] = src[band + i];
      }
    }
  }
  if (remainder > 0)
  {
    memcpy(dst + stride * element_count, src + stride * element_count, remainder);
  }
}

} // namespace points::converter
