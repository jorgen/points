#ifndef POINTS_CAMERA_P_H
#define POINTS_CAMERA_P_H

#include "glm_include.h"

namespace points
{
  namespace render
  {
    struct camera
    {
      glm::vec3 eye = glm::vec3(0, 0, 0);
      glm::vec3 center = glm::vec3(0, 0, 1);
      glm::vec3 up = glm::vec3(0, 1, 0);

      float fov = 45;
      float aspect = 16/9;
      float near = 0;
      float far = 1000;

      glm::mat4 view;
      glm::mat4 perspective;
      
      bool view_dirty = true;
      bool perspective_dirty = true;
      bool view_inverse_dirty = false;
      bool perspective_inverse_dirty = false;
    };
  }
}

#endif
