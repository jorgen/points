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

#include "conversion_types_p.h"
#include "morton_p.h"

namespace points
{
namespace converter
{

template<typename T>
inline void convert_morton_to_pos(const double (&scale)[3], const double (&offset)[3], const morton::morton_t<T> &morton, double (&pos)[3])
{
  using c_uint_t = typename std::remove_reference<decltype(morton.data[0])>::type;
  using uint_t = typename std::remove_const<c_uint_t>::type;
  uint_t ipos[3];
  morton::decode(morton, ipos);
  pos[0] = double(ipos[0]) * scale[0] + offset[0];
  pos[1] = double(ipos[1]) * scale[1] + offset[1];
  pos[2] = double(ipos[2]) * scale[2] + offset[2];
}

template<typename T>
inline void convert_pos_to_morton(const double (&scale)[3], const double (&offset)[3], const double(&pos)[3], morton::morton_t<T> &morton)
{
  using c_uint_t = typename std::remove_reference<decltype(morton.data[0])>::type;
  using uint_t = typename std::remove_const<c_uint_t>::type;
  uint_t ipos[3];
  ipos[0] = uint_t(round((pos[0] - offset[0]) / scale[0]));
  ipos[1] = uint_t(round((pos[1] - offset[1]) / scale[1]));
  ipos[2] = uint_t(round((pos[2] - offset[2]) / scale[2]));
  encode(ipos, morton);
}

template<typename T>
inline bool convert_world_morton_to_local(const tree_global_state_t &state, const morton::morton64_t &world, const points_t &p,  morton::morton_t<T> &morton)
{
  double pos[3];
  convert_morton_to_pos(state.scale, state.offset, world, pos);
  pos[0] -= p.header.offset[0];
  pos[1] -= p.header.offset[1];
  pos[2] -= p.header.offset[2];
  if (pos[0] < 0.0 || pos[1] < 0.0 || pos[2] < 0.0)
  {
    return false;
  }
  
  using c_uint_t = typename std::remove_reference<decltype(morton.data[0])>::type;
  using uint_t = typename std::remove_const<c_uint_t>::type;
  uint_t ipos[3];
  ipos[0] = uint_t(round(pos[0] / p.header.scale[0]));
  ipos[1] = uint_t(round(pos[1] / p.header.scale[1]));
  ipos[2] = uint_t(round(pos[2] / p.header.scale[2]));
  encode(ipos, morton);
  return true;
}

template<typename T>
inline void convert_local_morton_to_world(const points_t &p, const morton::morton_t<T> &local, const tree_global_state_t &state, morton::morton64_t &world)
{
  double pos[3];
  convert_morton_to_pos(p.header.scale, p.header.offset, local, pos);
  assert(pos[0] >= 0.0 && pos[1] >= 0.0 && pos[2] >= 0.0);
  convert_pos_to_morton(state.scale, state.offset, pos, world);
}

inline void convert_world_to_pos(const tree_global_state_t &state, const morton::morton64_t &world, double (&pos)[3])
{
  convert_morton_to_pos(state.scale, state.offset, world, pos);
}

inline void convert_pos_to_world(const tree_global_state_t &state, const double(&pos)[3], morton::morton64_t &world)
{
  convert_pos_to_morton(state.scale, state.offset, pos, world);
}
}
} // namespace points
