#include <points/render/camera.h>
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

    void camera_look_at(struct camera* camera, float* eye, float* center, float* up)
    {
      camera->eye = glm::make_vec3(eye);
      camera->center = glm::make_vec3(center);
      camera->up = glm::make_vec3(up);
      camera->view_dirty = true;
      camera->view_inverse_dirty = false;
    }
    void camera_set_view_matrix(struct camera* camera, float* data)
    {
      camera->view = glm::make_mat4(data);
      camera->view_dirty = false;
      camera->view_inverse_dirty = false;
    }
    void camera_set_perspective_matrix(struct camera* camera, float* data)
    {
      camera->perspective = glm::make_mat4(data);
      camera->perspective_dirty = false;
      camera->perspective_inverse_dirty = true;
    }
    void camera_set_perspective(struct camera* camera, float fov, float width, float height, float near, float far)
    {
      camera->fov = fov;
      camera->aspect = width / height;
      camera->near = near;
      camera->far = far;
      camera->perspective_dirty = true;
      camera->perspective_inverse_dirty = false;
    }
  }
}
