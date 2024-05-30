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

#include "glm_include.hpp"
#include "frustum.hpp"

#ifdef _MSC_VER
#define M_PI 3.14159265358979323846
#endif

#include <cmath>

namespace points
{
namespace render
{
struct camera_t
{
  glm::dmat4 view;
  glm::dmat4 projection;
};

inline frustum_t make_frustum(const glm::dmat4 &view_perspective)
{
  glm::dvec4 perspective_vec(view_perspective[0][3], view_perspective[1][3], view_perspective[2][3],
                             view_perspective[3][3]);

  frustum_t frustum;
  frustum.planes[frustum_t::left].x = view_perspective[0][3] + view_perspective[0][0];
  frustum.planes[frustum_t::left].y = view_perspective[1][3] + view_perspective[1][0];
  frustum.planes[frustum_t::left].z = view_perspective[2][3] + view_perspective[2][0];
  frustum.planes[frustum_t::left].w = view_perspective[3][3] + view_perspective[3][0];

  frustum.planes[frustum_t::right].x = view_perspective[0][3] - view_perspective[0][0];
  frustum.planes[frustum_t::right].y = view_perspective[1][3] - view_perspective[1][0];
  frustum.planes[frustum_t::right].z = view_perspective[2][3] - view_perspective[2][0];
  frustum.planes[frustum_t::right].w = view_perspective[3][3] - view_perspective[3][0];

  frustum.planes[frustum_t::bottom].x = view_perspective[0][3] + view_perspective[0][1];
  frustum.planes[frustum_t::bottom].y = view_perspective[1][3] + view_perspective[1][1];
  frustum.planes[frustum_t::bottom].z = view_perspective[2][3] + view_perspective[2][1];
  frustum.planes[frustum_t::bottom].w = view_perspective[3][3] + view_perspective[3][1];

  frustum.planes[frustum_t::top].x = view_perspective[0][3] - view_perspective[0][1];
  frustum.planes[frustum_t::top].y = view_perspective[1][3] - view_perspective[1][1];
  frustum.planes[frustum_t::top].z = view_perspective[2][3] - view_perspective[2][1];
  frustum.planes[frustum_t::top].w = view_perspective[3][3] - view_perspective[3][1];

  frustum.planes[frustum_t::near].x = view_perspective[0][3] + view_perspective[0][2];
  frustum.planes[frustum_t::near].y = view_perspective[1][3] + view_perspective[1][2];
  frustum.planes[frustum_t::near].z = view_perspective[2][3] + view_perspective[2][2];
  frustum.planes[frustum_t::near].w = view_perspective[3][3] + view_perspective[3][2];

  frustum.planes[frustum_t::far].x = view_perspective[0][3] - view_perspective[0][2];
  frustum.planes[frustum_t::far].y = view_perspective[1][3] - view_perspective[1][2];
  frustum.planes[frustum_t::far].z = view_perspective[2][3] - view_perspective[2][2];
  frustum.planes[frustum_t::far].w = view_perspective[3][3] - view_perspective[3][2];
  for (auto &plane : frustum.planes)
  {
    plane_normalize(plane);
  }
  return frustum;
}

template <typename R>
inline R to_radians(R degrees)
{
  return degrees / (R(180.0) / R(M_PI));
}

template <typename R>
inline R to_degrees(R radians)
{
  return radians * (R(180.0) / R(M_PI));
}

namespace camera_manipulator
{
struct arcball_t
{
  camera_t *camera;
  glm::dvec3 center;
  glm::dmat4 inverse_view;
  glm::dmat4 initial_rot;
  double inverse_yaw;
  double yaw;
  double pitch;
  double roll;
};

struct fps_t
{
  camera_t *camera;
  glm::dmat4 inverse_view;
  double yaw;
  double pitch;
  double roll;
};

} // namespace camera_manipulator
} // namespace render
} // namespace points
