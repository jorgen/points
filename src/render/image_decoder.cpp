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
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "image_decoder_p.h"

std::unique_ptr<uint8_t, decltype(&free)> load_image(uint8_t *data, int data_size, int &x, int &h, int &channels)
{
  uint8_t *image = stbi_load_from_memory(data, data_size, &x, &h, &channels, 0);
  return std::unique_ptr<uint8_t, decltype(&free)>(image, &free);
}
