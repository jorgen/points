/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2020  Jørgen Lind
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

#include <points/render/aabb.h>
#include "glm_include.hpp"

namespace points
{
namespace render
{
inline glm::dvec3 aabb_center(const aabb_t &aabb)
{
  double half_x = aabb.max[0] - aabb.min[0];
  double half_y = aabb.max[1] - aabb.min[1];
  double half_z = aabb.max[2] - aabb.min[2];
  return glm::dvec3(aabb.min[0] + half_x, aabb.min[1] + half_y, aabb.min[2] + half_z);
}

} // namespace render
} // namespace points
