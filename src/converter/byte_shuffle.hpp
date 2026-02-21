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
#pragma once

#include <cstdint>

namespace points::converter
{

void byte_shuffle(const uint8_t *src, uint8_t *dst, uint32_t total_size, uint32_t typesize, uint32_t component_count);
void byte_unshuffle(const uint8_t *src, uint8_t *dst, uint32_t total_size, uint32_t typesize, uint32_t component_count);

} // namespace points::converter
