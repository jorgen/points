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
#ifndef POINTS_FRUSTUM_P_H
#define POINTS_FRUSTUM_P_H

#include "glm_include.h"

namespace points
{
  namespace render
  {
    typedef glm::dvec4 plane;
    struct frustum
    {
      enum plane_names
      {
        left,
        right,
        bottom,
        top,
        near,
        far
      };
      plane planes[6];
    };

    inline void plane_normalize(plane& plane)
    {
      //glm::normalize(plane);
      double mag = sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
      plane.x = plane.x / mag;
      plane.y = plane.y / mag;
      plane.z = plane.z / mag;
      plane.w = plane.w / mag;
    }

    template<typename T>
    inline double plane_distance(const plane& plane, const T& pos)
    {
      return plane.x * pos[0] + plane.y * pos[0] + plane.z * pos[0] + plane.w;
    }
    
    inline bool frustum_contains_aabb(const frustum& frustum, const aabb& aabb)
    {
      for (int i = 0; i < 6; i++)
      {
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.min[0], aabb.min[1], aabb.min[2])) < 0.0)
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.max[0], aabb.min[1], aabb.min[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.min[0], aabb.max[1], aabb.min[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.min[0], aabb.min[1], aabb.max[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.max[0], aabb.max[1], aabb.min[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.max[0], aabb.min[1], aabb.max[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.min[0], aabb.max[1], aabb.max[2])) < 0.0) 
          return false;
        if (plane_distance(frustum.planes[i], glm::dvec3(aabb.max[0], aabb.max[1], aabb.max[2])) < 0.0) 
          return false;
      }
      return true;
    }


  }
}

#endif
