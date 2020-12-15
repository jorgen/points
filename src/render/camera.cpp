#include <points/render/camera.h>
#include <points/render/aabb.h>
#include "camera_p.h"

#include "glm_include.h"

#include <cmath>

#include <fmt/printf.h>

namespace points
{
namespace render
{
struct camera *camera_create()
{
  auto *ret = new camera();
  ret->projection = glm::perspective(45.0, 16.0 / 9.0, 0.01, 1000.0);
  ret->view = glm::mat4(1);
  return ret;
}

void camera_destroy(struct camera *camera)
{
  delete camera;
}

void camera_look_at(struct camera *camera, const double eye[3], const double center[3], const double up[3])
{
  camera->view = glm::lookAt(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up));
}

void camera_look_at_aabb(struct camera *camera, struct aabb *aabb, const double direction[3], const double up[3])
{
  double half_x = (aabb->max[0] - aabb->min[0]) / 2;
  double half_y = (aabb->max[1] - aabb->min[1]) / 2;
  double half_z = (aabb->max[2] - aabb->min[2]) / 2;
  glm::dvec3 aabb_center;
  aabb_center[0] = aabb->min[0] + half_x;
  aabb_center[1] = aabb->min[1] + half_y;
  aabb_center[2] = aabb->min[2] + half_z;
  double fov = 2.0 * atan(1.0 / camera->projection[1][1]);
  double distance = half_x / tan(fov / 2.0);
  glm::dvec3 direction_vector = glm::normalize(glm::make_vec3(direction));
  camera->view = glm::lookAt(aabb_center + (direction_vector * distance * 2.0), aabb_center, glm::make_vec3(up));
}

void camera_get_view_matrix(struct camera *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->view), sizeof(camera->view));
}

void camera_set_view_matrix(struct camera *camera, const double data[16])
{
  camera->view = glm::make_mat4(data);
}

void camera_get_perspective_matrix(struct camera *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->projection), sizeof(camera->projection));
}

void camera_set_perspective_matrix(struct camera *camera, const double data[16])
{
  camera->projection = glm::make_mat4(data);
}

void camera_set_perspective(struct camera *camera, double fov, double width, double height, double near, double far)
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

namespace camera_manipulator
{

  /*struct fps;
  {
  }
  */

struct arcball *arcball_create(struct camera *camera, const double center[3])
{
  auto ret = new arcball;
  ret->camera = camera;
  ret->center = glm::dvec3(center[0], center[1], center[2]);

  arcball_reset(ret);
  return ret;
}

void arcball_destroy(struct arcball *arcball)  
{
  delete arcball;
}

void arcball_reset(struct arcball *arcball)
{
  arcball->inverse_view = glm::inverse(arcball->camera->view);
  arcball->initial_rot = glm::translate(glm::dmat4(1), arcball->center);
}

void arcball_rotate(struct arcball *arcball, float normalized_dx, float normalized_dy, float normalized_dz)
{
  auto &view_inverse = arcball->inverse_view;

  auto qrot = glm::rotate(glm::dquat(1.0, 0.0, 0.0, 0.0) , -normalized_dx * M_PI, glm::dvec3(view_inverse[1]));
  qrot = glm::rotate(qrot, -normalized_dy * M_PI, glm::dvec3(view_inverse[0]));
  qrot = glm::rotate(qrot, normalized_dz * M_PI, glm::dvec3(view_inverse[2]));

  auto arcball_rot = arcball->initial_rot * glm::mat4_cast(qrot);

  arcball_rot = glm::translate(arcball_rot, -arcball->center);

  glm::dvec3 translate = glm::dvec3(view_inverse[3]);
  view_inverse = glm::translate(view_inverse, -glm::dvec3(translate));
  view_inverse = arcball_rot * view_inverse;
  view_inverse = glm::translate(view_inverse, glm::dvec3(translate));

  arcball->camera->view = glm::inverse(view_inverse);
}
  
void arcball_zoom(struct arcball *arcball, float normalized_zoom)
{
  auto &view_inverse = arcball->inverse_view;

  auto distance = glm::dvec3(view_inverse[3]) - arcball->center;
  auto move_distance = glm::length(distance) * normalized_zoom;
  auto translate = glm::dvec3(view_inverse[2]) * move_distance;
  view_inverse[3] += glm::dvec4(translate, 0.0);
  arcball->camera->view = glm::inverse(view_inverse);
}
} // namespace camera_manipulator
} // namespace render
} // namespace points
