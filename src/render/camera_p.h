#ifndef POINTS_CAMERA_P_H
#define POINTS_CAMERA_P_H

#include "glm_include.h"

#define M_PI       3.14159265358979323846   // pi

#include <cmath>

namespace points
{
  namespace render
  {
    struct camera
    {
      glm::dvec3 eye = glm::dvec3(0, 0, 0);
      glm::dvec3 center = glm::dvec3(0, 0, 1);
      glm::dvec3 up = glm::dvec3(0, 1, 0);

      double fov = 45;
      double aspect = 16/9;
      double near = 0;
      double far = 1000;

      glm::dmat4 view;
      glm::dmat4 perspective;
      
      bool view_dirty = true;
      bool perspective_dirty = true;
      bool view_inverse_dirty = false;
      bool perspective_inverse_dirty = false;

      void updateInverseProjectionProperties()
      {
        if (!perspective_inverse_dirty || perspective_dirty)
          return;
        fov = 2.0 * atan(1.0 / perspective[1][1]) * 180.0 / M_PI;
        aspect = perspective[1][1] / perspective[0][0];
        near = perspective[2][3] / (perspective[2][2] - 1.0);
        far = perspective[2][3] / (perspective[2][2] + 1.0);
      }
    };
  }
}

#endif
