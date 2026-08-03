/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#ifndef DEW_CAMERA_H
#define DEW_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dew/render/export.h>

struct dew_camera_t;
DEW_RENDER_EXPORT struct dew_camera_t *dew_camera_create(void);
DEW_RENDER_EXPORT void dew_camera_destroy(struct dew_camera_t *camera);
DEW_RENDER_EXPORT void dew_camera_look_at(struct dew_camera_t *camera, const double eye[3], const double center[3], const double up[3]);
//= in: aabb
DEW_RENDER_EXPORT void dew_camera_look_at_aabb(struct dew_camera_t *camera, struct dew_aabb_t *aabb, const double direction[3], const double up[3]);
DEW_RENDER_EXPORT void dew_camera_get_view_matrix(struct dew_camera_t *camera, double data[16]);
DEW_RENDER_EXPORT void dew_camera_set_view_matrix(struct dew_camera_t *camera, const double data[16]);
DEW_RENDER_EXPORT void dew_camera_get_perspective_matrix(struct dew_camera_t *camera, double data[16]);
DEW_RENDER_EXPORT void dew_camera_set_perspective_matrix(struct dew_camera_t *camera, const double data[16]);
DEW_RENDER_EXPORT void dew_camera_set_perspective(struct dew_camera_t *camera, double fov, double width, double height, double near, double far);
DEW_RENDER_EXPORT void dew_camera_perspective_properties(struct dew_camera_t *camera, double *fov, double *aspect,
                                                        double *near, double *far);
DEW_RENDER_EXPORT void dew_camera_get_eye(struct dew_camera_t *camera, double eye[3]);
DEW_RENDER_EXPORT void dew_camera_get_forward(struct dew_camera_t *camera, double forward[3]);

struct dew_arcball_t;
DEW_RENDER_EXPORT struct dew_arcball_t *dew_arcball_create(struct dew_camera_t *camera, const double center[3]);
DEW_RENDER_EXPORT void dew_arcball_destroy(struct dew_arcball_t *arcball);
DEW_RENDER_EXPORT void dew_arcball_reset(struct dew_arcball_t *arcball);
DEW_RENDER_EXPORT void dew_arcball_detect_upside_down(struct dew_arcball_t *arcball);
DEW_RENDER_EXPORT void dew_arcball_rotate(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy, float normalized_dz);
DEW_RENDER_EXPORT void dew_arcball_pan(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy);
DEW_RENDER_EXPORT void dew_arcball_pan_ground(struct dew_arcball_t *arcball, float normalized_dx, float normalized_dy);
DEW_RENDER_EXPORT void dew_arcball_dolly(struct dew_arcball_t *arcball, float normalized_dz);
DEW_RENDER_EXPORT void dew_arcball_zoom(struct dew_arcball_t *arcball, float normalized_zoom);
DEW_RENDER_EXPORT void dew_arcball_set_up_axis(struct dew_arcball_t *arcball, const double up[3]);
DEW_RENDER_EXPORT void dew_arcball_get_up_axis(struct dew_arcball_t *arcball, double up[3]);
DEW_RENDER_EXPORT void dew_arcball_get_center(struct dew_arcball_t *arcball, double center[3]);

struct dew_fps_t;
DEW_RENDER_EXPORT struct dew_fps_t *dew_fps_create(struct dew_camera_t *camera);
DEW_RENDER_EXPORT void dew_fps_destroy(struct dew_fps_t *fps);
DEW_RENDER_EXPORT void dew_fps_reset(struct dew_fps_t *fps);
DEW_RENDER_EXPORT void dew_fps_rotate(struct dew_fps_t *fps, float normalized_dx, float normalized_dy, float normalized_dz);
DEW_RENDER_EXPORT void dew_fps_move(struct dew_fps_t *fps, float dx, float dy, float dz);

#ifdef __cplusplus
}
#endif
#endif // DEW_CAMERA_H
