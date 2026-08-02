/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jorgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#include <dew/render/camera.h>
#include <dew/render/aabb.h>
#include "camera.hpp"

#include "glm_include.hpp"

#include <cmath>

#include <fmt/printf.h>

struct dew_camera_t *dew_camera_create()
{
  auto *ret = new dew_camera_t();
  ret->projection = glm::perspective(45.0, 16.0 / 9.0, 0.01, 1000.0);
  ret->view = glm::mat4(1);
  return ret;
}

void dew_camera_destroy(struct dew_camera_t *camera)
{
  delete camera;
}

void dew_camera_look_at(struct dew_camera_t *camera, const double eye[3], const double center[3], const double up[3])
{
  camera->view = glm::lookAt(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up));
}

void dew_camera_look_at_aabb(struct dew_camera_t *camera, struct dew_aabb_t *aabb, const double direction[3], const double up[3])
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

void dew_camera_get_view_matrix(struct dew_camera_t *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->view), sizeof(camera->view));
}

void dew_camera_set_view_matrix(struct dew_camera_t *camera, const double data[16])
{
  camera->view = glm::make_mat4(data);
}

void dew_camera_get_perspective_matrix(struct dew_camera_t *camera, double data[16])
{
  memcpy(data, glm::value_ptr(camera->projection), sizeof(camera->projection));
}

void dew_camera_set_perspective_matrix(struct dew_camera_t *camera, const double data[16])
{
  camera->projection = glm::make_mat4(data);
}

void dew_camera_set_perspective(struct dew_camera_t *camera, double fov, double width, double height, double near, double far)
{
  camera->projection = glm::perspective(fov, width / height, near, far);
}

void dew_camera_perspective_properties(struct dew_camera_t *camera, double *fov, double *aspect, double *near, double *far)
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

static glm::dvec3 arcball_forward_dir(double yaw, double pitch, const glm::dvec3 &up)
{
  double cp = cos(pitch);
  double sp = sin(pitch);
  double sy = sin(yaw);
  double cy = cos(yaw);

  double ax = fabs(up.x), ay = fabs(up.y), az = fabs(up.z);
  double sign = (up.x + up.y + up.z) < 0.0 ? -1.0 : 1.0;

  if (ay >= ax && ay >= az)
    return glm::dvec3(cp * sy, sign * sp, cp * cy);
  if (az >= ax && az >= ay)
    return glm::dvec3(cp * sy, cp * cy, sign * sp);
  return glm::dvec3(sign * sp, cp * sy, cp * cy);
}

static void arcball_update_view(dew_arcball_t *arcball)
{
  glm::dvec3 forward = arcball_forward_dir(arcball->yaw, arcball->pitch, arcball->up);
  glm::dvec3 eye = arcball->center - forward * arcball->distance;
  arcball->camera->view = glm::lookAt(eye, arcball->center, arcball->up);
}

static void arcball_extract_angles(dew_arcball_t *arcball)
{
  glm::dvec3 eye = glm::dvec3(glm::inverse(arcball->camera->view)[3]);
  glm::dvec3 diff = arcball->center - eye;
  arcball->distance = glm::length(diff);
  if (arcball->distance > 0.0)
  {
    glm::dvec3 f = diff / arcball->distance;
    double ax = fabs(arcball->up.x), ay = fabs(arcball->up.y), az = fabs(arcball->up.z);
    double sign = (arcball->up.x + arcball->up.y + arcball->up.z) < 0.0 ? -1.0 : 1.0;

    if (ay >= ax && ay >= az)
    {
      arcball->pitch = asin(glm::clamp(sign * f.y, -1.0, 1.0));
      arcball->yaw = atan2(f.x, f.z);
    }
    else if (az >= ax && az >= ay)
    {
      arcball->pitch = asin(glm::clamp(sign * f.z, -1.0, 1.0));
      arcball->yaw = atan2(f.x, f.y);
    }
    else
    {
      arcball->pitch = asin(glm::clamp(sign * f.x, -1.0, 1.0));
      arcball->yaw = atan2(f.y, f.z);
    }
  }
  else
  {
    arcball->distance = 0.01;
    arcball->pitch = 0.0;
    arcball->yaw = 0.0;
  }
}

// Snapshot the current view as the "home" the reset button returns to.
static void arcball_capture_home(dew_arcball_t *arcball)
{
  arcball->home_center = arcball->center;
  arcball->home_up = arcball->up;
  arcball->home_distance = arcball->distance;
  arcball->home_yaw = arcball->yaw;
  arcball->home_pitch = arcball->pitch;
}

struct dew_arcball_t *dew_arcball_create(struct dew_camera_t *camera, const double center[3])
{
  auto ret = new dew_arcball_t;
  ret->camera = camera;
  ret->center = glm::make_vec3(center);
  ret->up = glm::dvec3(0, 1, 0);
  arcball_extract_angles(ret);
  arcball_update_view(ret);
  arcball_capture_home(ret);
  return ret;
}

void dew_arcball_destroy(struct dew_arcball_t *arcball)
{
  delete arcball;
}

void dew_arcball_reset(struct dew_arcball_t *arcball)
{
  arcball->center = arcball->home_center;
  arcball->up = arcball->home_up;
  arcball->distance = arcball->home_distance;
  arcball->yaw = arcball->home_yaw;
  arcball->pitch = arcball->home_pitch;
  arcball_update_view(arcball);
}

void dew_arcball_detect_upside_down(struct dew_arcball_t *arcball)
{
  (void)arcball;
}

static double normalize_angle(double angle)
{
  constexpr double two_pi = M_PI * 2;
  return angle - two_pi * std::floor((angle + M_PI) / two_pi);
}

void dew_arcball_rotate(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy, float normalized_dz)
{
  (void)normalized_dz;
  arcball->yaw += normalized_dx * M_PI;
  arcball->pitch += normalized_dy * M_PI;
  constexpr double max_pitch = 89.0 * M_PI / 180.0;
  arcball->pitch = glm::clamp(arcball->pitch, -max_pitch, max_pitch);
  arcball_update_view(arcball);
}

void dew_arcball_pan(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy)
{
  glm::dvec3 forward = arcball_forward_dir(arcball->yaw, arcball->pitch, arcball->up);
  glm::dvec3 right = glm::normalize(glm::cross(forward, arcball->up));
  glm::dvec3 cam_up = glm::cross(right, forward);
  arcball->center += right * (double(-normalized_dx) * arcball->distance)
                   + cam_up * (double(-normalized_dy) * arcball->distance);
  arcball_update_view(arcball);
}

void dew_arcball_pan_ground(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy)
{
  glm::dvec3 forward = arcball_forward_dir(arcball->yaw, 0.0, arcball->up);
  glm::dvec3 right = glm::normalize(glm::cross(forward, arcball->up));
  arcball->center += right * (double(-normalized_dx) * arcball->distance)
                   + forward * (double(-normalized_dy) * arcball->distance);
  arcball_update_view(arcball);
}

void dew_arcball_dolly(struct dew_arcball_t *arcball, float normalized_dz)
{
  glm::dvec3 forward = arcball_forward_dir(arcball->yaw, arcball->pitch, arcball->up);
  arcball->center += forward * (double(-normalized_dz) * arcball->distance);
  arcball_update_view(arcball);
}

void dew_arcball_zoom(struct dew_arcball_t *arcball, float normalized_zoom)
{
  arcball->distance *= (1.0 + normalized_zoom);
  arcball->distance = glm::max(arcball->distance, 0.01);
  arcball_update_view(arcball);
}

struct dew_fps_t *dew_fps_create(struct dew_camera_t *camera)
{
  dew_fps_t *ret = new dew_fps_t();
  ret->camera = camera;
  dew_fps_reset(ret);
  return ret;
}

void dew_fps_destroy(struct dew_fps_t *fps)
{
  delete fps;
}

void dew_fps_reset(struct dew_fps_t *fps)
{
  fps->inverse_view = glm::inverse(fps->camera->view);
  fps->pitch = 0.0;
  fps->yaw = 0.0;
  fps->roll = 0.0;
}

void dew_fps_rotate(struct dew_fps_t *fps, float normalized_dx, float normalized_dy, float normalized_dz)
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

void dew_fps_move(struct dew_fps_t *fps, float dx, float dy, float dz)
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

void dew_arcball_set_up_axis(struct dew_arcball_t *arcball, const double up[3])
{
  arcball->up = glm::make_vec3(up);
  arcball_extract_angles(arcball);
  arcball_update_view(arcball);
  arcball_capture_home(arcball);
}

void dew_arcball_get_up_axis(struct dew_arcball_t *arcball, double up[3])
{
  memcpy(up, glm::value_ptr(arcball->up), 3 * sizeof(double));
}

void dew_arcball_get_center(struct dew_arcball_t *arcball, double center[3])
{
  memcpy(center, glm::value_ptr(arcball->center), 3 * sizeof(double));
}

void dew_camera_get_eye(struct dew_camera_t *camera, double eye[3])
{
  glm::dvec3 e = glm::dvec3(glm::inverse(camera->view)[3]);
  memcpy(eye, glm::value_ptr(e), 3 * sizeof(double));
}

void dew_camera_get_forward(struct dew_camera_t *camera, double forward[3])
{
  glm::dmat4 inv = glm::inverse(camera->view);
  glm::dvec3 f = -glm::dvec3(inv[2]);
  f = glm::normalize(f);
  memcpy(forward, glm::value_ptr(f), 3 * sizeof(double));
}
