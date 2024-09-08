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

#include "glm_include.hpp"
#include <array>

namespace points::render
{

enum class frustum_intersection_t
{
  outside,
  intersects,
  inside
};

struct frustum_t
{
  std::array<glm::dvec4, 6> planes;

  void update(const glm::dmat4 &view_projection_matrix)
  {
    // Extract frustum planes from the view-projection matrix
    planes[0] = glm::dvec4(view_projection_matrix[0][3] + view_projection_matrix[0][0], view_projection_matrix[1][3] + view_projection_matrix[1][0], view_projection_matrix[2][3] + view_projection_matrix[2][0],
                           view_projection_matrix[3][3] + view_projection_matrix[3][0]); // Left

    planes[1] = glm::dvec4(view_projection_matrix[0][3] - view_projection_matrix[0][0], view_projection_matrix[1][3] - view_projection_matrix[1][0], view_projection_matrix[2][3] - view_projection_matrix[2][0],
                           view_projection_matrix[3][3] - view_projection_matrix[3][0]); // Right

    planes[2] = glm::dvec4(view_projection_matrix[0][3] + view_projection_matrix[0][1], view_projection_matrix[1][3] + view_projection_matrix[1][1], view_projection_matrix[2][3] + view_projection_matrix[2][1],
                           view_projection_matrix[3][3] + view_projection_matrix[3][1]); // Bottom

    planes[3] = glm::dvec4(view_projection_matrix[0][3] - view_projection_matrix[0][1], view_projection_matrix[1][3] - view_projection_matrix[1][1], view_projection_matrix[2][3] - view_projection_matrix[2][1],
                           view_projection_matrix[3][3] - view_projection_matrix[3][1]); // Top

    planes[4] = glm::dvec4(view_projection_matrix[0][3] + view_projection_matrix[0][2], view_projection_matrix[1][3] + view_projection_matrix[1][2], view_projection_matrix[2][3] + view_projection_matrix[2][2],
                           view_projection_matrix[3][3] + view_projection_matrix[3][2]); // Near

    planes[5] = glm::dvec4(view_projection_matrix[0][3] - view_projection_matrix[0][2], view_projection_matrix[1][3] - view_projection_matrix[1][2], view_projection_matrix[2][3] - view_projection_matrix[2][2],
                           view_projection_matrix[3][3] - view_projection_matrix[3][2]); // Far

    // Normalize the planes
    for (auto &plane : planes)
    {
      double length = glm::length(glm::dvec3(plane));
      plane /= length;
    }
  }

  [[nodiscard]] frustum_intersection_t test_aabb(const glm::dvec3 &min, const glm::dvec3 &max) const
  {
    bool intersecting = false;

    for (const auto &plane : planes)
    {
      glm::dvec3 positive_vertex = min;
      glm::dvec3 negative_vertex = max;

      if (plane.x >= 0)
      {
        positive_vertex.x = max.x;
        negative_vertex.x = min.x;
      }
      if (plane.y >= 0)
      {
        positive_vertex.y = max.y;
        negative_vertex.y = min.y;
      }
      if (plane.z >= 0)
      {
        positive_vertex.z = max.z;
        negative_vertex.z = min.z;
      }

      // Check if the positive vertex is outside the frustum
      if (glm::dot(glm::dvec3(plane), positive_vertex) + plane.w < 0)
      {
        return frustum_intersection_t::outside;
      }

      // Check if the negative vertex is inside the frustum
      if (glm::dot(glm::dvec3(plane), negative_vertex) + plane.w < 0)
      {
        intersecting = true;
      }
    }

    return intersecting ? frustum_intersection_t::intersects : frustum_intersection_t::inside;
  }
};
} // namespace points::render
