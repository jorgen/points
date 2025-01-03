/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#ifndef POINTS_CAMERA_H
#define POINTS_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <points/render/export.h>


namespace points::render
{
  struct camera_t;
  POINTS_RENDER_EXPORT struct camera_t *camera_create();
  POINTS_RENDER_EXPORT void camera_destroy(struct camera_t *camera);
  POINTS_RENDER_EXPORT void camera_look_at(struct camera_t *camera, const double eye[3], const double center[3], const double up[3]);
  POINTS_RENDER_EXPORT void camera_look_at_aabb(struct camera_t *camera, struct aabb_t *aabb, const double direction[3], const double up[3]);
  POINTS_RENDER_EXPORT void camera_get_view_matrix(struct camera_t *camera, double data[16]);
  POINTS_RENDER_EXPORT void camera_set_view_matrix(struct camera_t *camera, const double data[16]);
  POINTS_RENDER_EXPORT void camera_get_perspective_matrix(struct camera_t *camera, double data[16]);
  POINTS_RENDER_EXPORT void camera_set_perspective_matrix(struct camera_t *camera, const double data[16]);
  POINTS_RENDER_EXPORT void camera_set_perspective(struct camera_t *camera, double fov, double width, double height, double near, double far);
  POINTS_RENDER_EXPORT void camera_perspective_properties(struct camera_t *camera, double *fov, double *aspect,
                                                          double *near, double *far);

  namespace camera_manipulator
  {
  struct arcball_t;
  POINTS_RENDER_EXPORT struct arcball_t *arcball_create(struct camera_t *camera, const double center[3]);
  POINTS_RENDER_EXPORT void arcball_destroy(struct arcball_t *arcball);
  POINTS_RENDER_EXPORT void arcball_reset(struct arcball_t *arcball);
  POINTS_RENDER_EXPORT void arcball_detect_upside_down(struct arcball_t *arcball);
  POINTS_RENDER_EXPORT void arcball_rotate(struct arcball_t *arcball, float normalized_dx, float normalized_dy, float normalized_dz);
  POINTS_RENDER_EXPORT void arcball_zoom(struct arcball_t *arcball, float normalized_zoom);

  struct fps_t;
  POINTS_RENDER_EXPORT struct fps_t *fps_create(struct camera_t *camera);
  POINTS_RENDER_EXPORT void fps_destroy(struct fps_t *fps);
  POINTS_RENDER_EXPORT void fps_reset(struct fps_t *fps);
  POINTS_RENDER_EXPORT void fps_rotate(struct fps_t *fps, float normalized_dx, float normalized_dy, float normalized_dz);
  POINTS_RENDER_EXPORT void fps_move(struct fps_t *fps, float dx, float dy, float dz);

  } // namespace camera_manipulator
  } // namespace points::render


#ifdef __cplusplus
}
#endif
#endif // POINTS_CAMERA_H
