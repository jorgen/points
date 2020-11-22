#include <points/render/camera.h>
#include <points/render/aabb.h>
#include "camera_p.h"

#include "glm_include.h"

namespace points
{
  namespace render
  {
    struct camera* camera_create()
    {
      return new camera();
    }

    void camera_destroy(struct camera* camera)
    {
      delete camera;
    }

    void camera_look_at(struct camera* camera, const double eye[3], const double center[3], const double up[3])
    {
      camera->view = glm::lookAt(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up));
    }
    
    void camera_look_at_aabb(struct camera* camera, struct aabb* aabb, const double direction[3], const double up[3])
    {
      double half_x = (aabb->min[0] - aabb->max[0]) / 2;
      double half_y = (aabb->min[1] - aabb->max[1]) / 2;
      double half_z = (aabb->min[2] - aabb->max[2]) / 2;
      glm::dvec3 aabb_center;
      aabb_center[0] = aabb->min[0] + half_x;
      aabb_center[1] = aabb->min[1] + half_y;
      aabb_center[2] = aabb->min[2] + half_z;
      double fov = 2.0 * atan(1.0 / camera->projection[1][1]);
      double distance = half_x / tan(fov / 2.0);
      glm::dvec3 direction_vector = glm::make_vec3(direction);
      camera_look_at(camera, glm::value_ptr(aabb_center + (direction_vector * distance)), glm::value_ptr(aabb_center), up);
    }

    void camera_set_view_matrix(struct camera* camera, const double data[16])
    {
      camera->view = glm::make_mat4(data);
    }

    void camera_set_perspective_matrix(struct camera* camera, const double data[16])
    {
      camera->projection = glm::make_mat4(data);
    }
    
    void camera_set_perspective(struct camera* camera, double fov, double width, double height, double near, double far)
    {
      camera->projection = glm::perspective(fov, width / height, near, far);
    }

    void camera_perspective_properties(struct camera *camera, double *fov, double *aspect, double *near, double *far)
    {
      if (fov)
        *fov = 2.0 * atan(1.0 / camera->projection[1][1]);
      if (aspect)
        *aspect = camera->projection[1][1] / camera->projection[0][0];
      if (near)
        *near = camera->projection[3][2] / (camera->projection[2][2] - 1.0);
      if (far)
        *far = camera->projection[3][2] / (camera->projection[2][2] + 1.0);
    }

  }
}
