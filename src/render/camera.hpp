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

#ifdef _MSC_VER
#define M_PI 3.14159265358979323846
#endif

struct points_camera_t
{
  glm::dmat4 view;
  glm::dmat4 projection;
};

namespace points::render
{
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
} // namespace points::render

struct points_arcball_t
{
  points_camera_t *camera;
  glm::dvec3 center;
  glm::dvec3 up;
  double distance;
  double yaw;
  double pitch;
};

struct points_fps_t
{
  points_camera_t *camera;
  glm::dmat4 inverse_view;
  double yaw;
  double pitch;
  double roll;
};
