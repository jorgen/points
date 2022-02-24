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
#pragma once

#include "conversion_types.hpp"
#include "morton.hpp"

namespace points
{
namespace converter
{

template<typename T, size_t C>
inline void convert_morton_to_pos(const double (&scale)[3], const double (&offset)[3], const morton::morton_t<T,C> &morton, double (&pos)[3])
{
  uint64_t ipos[3];
  morton::decode(morton, ipos);
  pos[0] = double(ipos[0]) * scale[0] + offset[0];
  pos[1] = double(ipos[1]) * scale[1] + offset[1];
  pos[2] = double(ipos[2]) * scale[2] + offset[2];
}

template<typename T, size_t C>
inline void convert_pos_to_morton(const double scale, const double (&offset)[3], const double(&pos)[3], morton::morton_t<T,C> &morton)
{
  uint64_t ipos[3];
  ipos[0] = uint64_t(round((pos[0] - offset[0]) / scale));
  ipos[1] = uint64_t(round((pos[1] - offset[1]) / scale));
  ipos[2] = uint64_t(round((pos[2] - offset[2]) / scale));
  encode(ipos, morton);
}

template<typename T, size_t C>
inline void convert_world_morton_to_local(const morton::morton192_t &world, morton::morton_t<T, C> &morton)
{
  morton::morton_downcast(world, morton);
}

template<typename T, size_t C>
inline void convert_local_morton_to_world(const morton::morton_t<T, C> &local, const morton::morton192_t &min, morton::morton192_t &world)
{
  morton::morton_upcast(local, min, world);
}
}
} // namespace points
