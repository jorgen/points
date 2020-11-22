#ifndef POINTS_CAMERA_P_H
#define POINTS_CAMERA_P_H

#include "glm_include.h"
#include "frustum_p.h"

#define M_PI       3.14159265358979323846   // pi

#include <cmath>

namespace points
{
  namespace render
  {
    struct camera
    {
      glm::dmat4 view;
      glm::dmat4 projection;
    };

    inline frustum make_frustum(const glm::dmat4 &view_perspective)
    {
      struct frustum frustum;
      frustum.planes[frustum::left].x = view_perspective[3][0] + view_perspective[0][0];
      frustum.planes[frustum::left].y = view_perspective[3][1] + view_perspective[0][1];
      frustum.planes[frustum::left].z = view_perspective[3][2] + view_perspective[0][2];
      frustum.planes[frustum::left].w = view_perspective[3][3] + view_perspective[0][3];

      frustum.planes[frustum::right].x = view_perspective[3][0] - view_perspective[0][0];
      frustum.planes[frustum::right].y = view_perspective[3][1] - view_perspective[0][1];
      frustum.planes[frustum::right].z = view_perspective[3][2] - view_perspective[0][2];
      frustum.planes[frustum::right].w = view_perspective[3][3] - view_perspective[0][3];

      frustum.planes[frustum::bottom].x = view_perspective[3][0] + view_perspective[1][0];
      frustum.planes[frustum::bottom].y = view_perspective[3][1] + view_perspective[1][1];
      frustum.planes[frustum::bottom].z = view_perspective[3][2] + view_perspective[1][2];
      frustum.planes[frustum::bottom].w = view_perspective[3][3] + view_perspective[1][3];

      frustum.planes[frustum::top].x = view_perspective[3][0] - view_perspective[1][0];
      frustum.planes[frustum::top].y = view_perspective[3][1] - view_perspective[1][1];
      frustum.planes[frustum::top].z = view_perspective[3][2] - view_perspective[1][2];
      frustum.planes[frustum::top].w = view_perspective[3][3] - view_perspective[1][3];

      frustum.planes[frustum::near].x = view_perspective[3][0] + view_perspective[2][0];
      frustum.planes[frustum::near].y = view_perspective[3][1] + view_perspective[2][1];
      frustum.planes[frustum::near].z = view_perspective[3][2] + view_perspective[2][2];
      frustum.planes[frustum::near].w = view_perspective[3][3] + view_perspective[2][3];

      frustum.planes[frustum::far].x = view_perspective[3][0] - view_perspective[2][0];
      frustum.planes[frustum::far].y = view_perspective[3][1] - view_perspective[2][1];
      frustum.planes[frustum::far].z = view_perspective[3][2] - view_perspective[2][2];
      frustum.planes[frustum::far].w = view_perspective[3][3] - view_perspective[2][3];
      for (auto& plane : frustum.planes)
      {
        plane_normalize(plane);
      }
      return frustum;
    }

    template<typename R>
    inline R to_radians(R degrees)
    {
      return degrees / (R(180.0) / R(M_PI));
    }

    template<typename R>
    inline R to_degrees(R radians)
    {
      return radians * (R(180.0) / R(M_PI));
    }
  }
}

#endif
