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
#ifndef INTERNAL_GLM_INCLUDE_H
#define INTERNAL_GLM_INCLUDE_H

#pragma warning(push)
#pragma warning(disable : 4201)
#pragma warning(disable : 4127)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#pragma warning(pop)

#include <array>

template <size_t SIZE>
glm::vec<SIZE, glm::f64, glm::defaultp> to_glm(const std::array<double, SIZE> &a)
{
  glm::vec<SIZE, glm::f64, glm::defaultp> ret;
  static_assert(sizeof(ret) == sizeof(a));
  memcpy(&ret, &a, sizeof ret);
  return ret;
}

template <size_t SIZE>
glm::vec<SIZE, glm::f64, glm::defaultp> to_glm(const double (&a)[SIZE])
{
  glm::vec<SIZE, glm::f64, glm::defaultp> ret;
  static_assert(sizeof(ret) == sizeof(a));
  memcpy(&ret, &a, sizeof ret);
  return ret;
}

#endif
