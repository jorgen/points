/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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

// Shared io/gpu/fade lifecycle enums, used by both render_node_t and virtual_node_t (so a virtual node can
// reuse the exact same upload/eviction/crossfade state machine without a circular include).

namespace points::converter
{

enum class render_node_io_state : uint8_t
{
  none,
  loading,
  converting,
  loaded,
};

enum class render_node_gpu_state : uint8_t
{
  none,
  uploaded,
};

enum class render_node_fade_state : uint8_t
{
  fade_in,
  steady,
  fade_out,
};

} // namespace points::converter
