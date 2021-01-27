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
struct camera_t *camera_create()
{
  auto *ret = new camera_t();
  ret->projection = glm::perspective(45.0, 16.0 / 9.0, 0.01, 1000.0);
  ret->view = glm::mat4(1);
  return ret;
}

void camera_destroy(struct camera_t *camera)
{
  delete camera;
}

void camera_look_at(struct camera_t *camera, const double eye[3], const double center[3], const double up[3])
{
  camera->view = glm::lookAt(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up));
}

void camera_look_at_aabb(struct camera_t *camera, struct aabb_t *aabb, const double direction[3], const double up[3])
{
  double half_x = (aabb->max[0] - aabb->min[0]) / 2;
  double half_y = (aabb->max[1] - aabb->min[1]) / 2;
  double half_z = (aabb->max[2] - aabb->min[2]) / 2;
  glm::dvec3 aabb_center;
  aabb_center[0] = aabb->min[0] + half_x;
  aabb_center[1] = aabb->min[1] + half_y;
  aabb_center[2] = aabb->min[2] + half_z;

  double longest_half = std::max(half_x, std::max(half_y, half_z));
  double fov = 2.0 * atan(1.0 / camera->projection[1][1]);
  double distance = longest_half / tan(fov / 2.0);
  glm::dvec3 direction_vector = glm::normalize(glm::make_vec3(direction));
  camera->view = glm::lookAt(aabb_center + (direction_vector * distance * 2.0), aabb_center, glm::make_vec3(up));
}

void camera_get_view_matrix(struct camera_t *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->view), sizeof(camera->view));
}

void camera_set_view_matrix(struct camera_t *camera, const double data[16])
{
  camera->view = glm::make_mat4(data);
}

void camera_get_perspective_matrix(struct camera_t *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->projection), sizeof(camera->projection));
}

void camera_set_perspective_matrix(struct camera_t *camera, const double data[16])
{
  camera->projection = glm::make_mat4(data);
}

void camera_set_perspective(struct camera_t *camera, double fov, double width, double height, double near, double far)
{
  camera->projection = glm::perspective(fov, width / height, near, far);
}

void camera_perspective_properties(struct camera_t *camera, double *fov, double *aspect, double *near, double *far)
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
struct arcball_t *arcball_create(struct camera_t *camera, const double center[3])
{
  auto ret = new arcball_t;
  ret->camera = camera;
  ret->center = glm::make_vec3(center);

  arcball_reset(ret);
  return ret;
}

void arcball_destroy(struct arcball_t *arcball)
{
  delete arcball;
}

void arcball_reset(struct arcball_t *arcball)
{
  arcball->inverse_view = glm::inverse(arcball->camera->view);
  arcball->initial_rot = glm::translate(glm::dmat4(1), arcball->center);
  arcball->pitch = 0.0;
  arcball->yaw = 0.0;
  arcball->roll = 0.0;
  arcball_detect_upside_down(arcball);
}

void arcball_detect_upside_down(struct arcball_t *arcball)
{
  glm::vec3 up(0.0, 1.0, 0.0);
  glm::vec3 camup = glm::vec3(glm::inverse(arcball->camera->view)[1]);
  auto up_dot = glm::dot(camup, up);
  auto angle = up_dot / (glm::length(up) * glm::length(camup));
  arcball->inverse_yaw = angle < 0.0 ? -1.0 : 1.0;
}

static double normalize_angle(double angle)
{
  constexpr double two_pi = M_PI * 2;
  return angle - two_pi * std::floor((angle + M_PI) / two_pi);
}

void arcball_rotate(struct arcball_t *arcball, float normalized_dx, float normalized_dy, float normalized_dz)
{
  auto view_inverse = arcball->inverse_view;

  if (normalized_dx)
  {
    arcball->yaw += (-normalized_dx * arcball->inverse_yaw) * M_PI;
    arcball->yaw = normalize_angle(arcball->yaw);
  }

  if (normalized_dy)
  {
    arcball->pitch +=  -normalized_dy * M_PI;
    arcball->pitch = normalize_angle(arcball->pitch);
  }

  if (normalized_dz)
  {
    arcball->roll += normalized_dz * M_PI;
    arcball->roll = normalize_angle(arcball->roll);
  }

  auto qrot = glm::dquat(1.0, 0.0, 0.0, 0.0);
  if (arcball->yaw)
    qrot = glm::rotate(glm::dquat(1.0, 0.0, 0.0, 0.0), arcball->yaw, glm::dvec3(view_inverse[1])) * qrot;
  if (arcball->pitch)
    qrot = glm::rotate(qrot, arcball->pitch, glm::dvec3(view_inverse[0]));
  if (arcball->roll)
    qrot = glm::rotate(qrot, arcball->roll, glm::dvec3(view_inverse[2]));

  auto arcball_rot = arcball->initial_rot * glm::mat4_cast(qrot);

  arcball_rot = glm::translate(arcball_rot, -arcball->center);

  glm::dvec3 translate = glm::dvec3(view_inverse[3]);
  view_inverse = glm::translate(view_inverse, -glm::dvec3(translate));
  view_inverse = arcball_rot * view_inverse;
  view_inverse = glm::translate(view_inverse, glm::dvec3(translate));

  arcball->camera->view = glm::inverse(view_inverse);
}
  
void arcball_zoom(struct arcball_t *arcball, float normalized_zoom)
{
  auto view_inverse = glm::inverse(arcball->camera->view);

  auto distance = glm::dvec3(view_inverse[3]) - arcball->center;
  auto move_distance = glm::length(distance) * normalized_zoom;
  auto translate = glm::dvec3(view_inverse[2]) * move_distance;
  view_inverse[3] += glm::dvec4(translate, 0.0);
  arcball->camera->view = glm::inverse(view_inverse);
  arcball->inverse_view = view_inverse;
  arcball->pitch = 0.0;
  arcball->yaw = 0.0;
  arcball->roll = 0.0;
}

struct fps_t *fps_create(struct camera_t *camera)
{
  fps_t *ret = new fps_t();
  ret->camera = camera;
  fps_reset(ret);
  return ret;
}

void fps_destroy(struct fps_t *fps)
{
  delete fps;
}

void fps_reset(struct fps_t *fps)
{
  fps->inverse_view = glm::inverse(fps->camera->view);
  fps->pitch = 0.0;
  fps->yaw = 0.0;
  fps->roll = 0.0;
}

void fps_rotate(struct fps_t *fps, float normalized_dx, float normalized_dy, float normalized_dz)
{
  auto view_inverse = fps->inverse_view;
  if (normalized_dx)
  {
    fps->pitch += -normalized_dx * M_PI;
    fps->pitch = normalize_angle(fps->pitch);
  }

  if (normalized_dy)
  {
    fps->yaw +=  -normalized_dy * M_PI;
    fps->yaw = normalize_angle(fps->yaw);
  }

  if (normalized_dz)
  {
    fps->roll += normalized_dz * M_PI;
    fps->roll = normalize_angle(fps->roll);
  }

  auto qrot = glm::dquat(1.0, 0.0, 0.0, 0.0);
  if (fps->pitch)
    qrot = glm::rotate(qrot, fps->pitch, glm::dvec3(view_inverse[1]));
  if (fps->yaw)
    qrot = glm::rotate(qrot, fps->yaw, glm::dvec3(view_inverse[0]));
  if (fps->roll)
    qrot = glm::rotate(qrot, fps->roll, glm::dvec3(view_inverse[2]));

  glm::dvec4 translate = view_inverse[3];
  view_inverse[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);
  view_inverse = glm::mat4_cast(qrot) * view_inverse;
  view_inverse[3] = translate;

  fps->camera->view = glm::inverse(view_inverse);
}

void fps_move(struct fps_t *fps, float dx, float dy, float dz)
{
  auto view_inverse = glm::inverse(fps->camera->view);

  auto move = glm::dvec4(0.0);
  if (dx)
    move += glm::normalize(view_inverse[0]) * double(dx);
  if (dy)
    move += glm::normalize(view_inverse[1]) * double(dy);
  if (dz)
    move += glm::normalize(view_inverse[2]) * double(dz);

  view_inverse[3] += move;
  fps->camera->view = glm::inverse(view_inverse);
  fps->inverse_view = view_inverse;
  fps->pitch = 0.0;
  fps->yaw = 0.0;
  fps->roll = 0.0;
}

} // namespace camera_manipulator
} // namespace render
} // namespace points
