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

struct points_camera_t;
POINTS_RENDER_EXPORT struct points_camera_t *points_camera_create(void);
POINTS_RENDER_EXPORT void points_camera_destroy(struct points_camera_t *camera);
POINTS_RENDER_EXPORT void points_camera_look_at(struct points_camera_t *camera, const double eye[3], const double center[3], const double up[3]);
POINTS_RENDER_EXPORT void points_camera_look_at_aabb(struct points_camera_t *camera, struct points_aabb_t *aabb, const double direction[3], const double up[3]);
POINTS_RENDER_EXPORT void points_camera_get_view_matrix(struct points_camera_t *camera, double data[16]);
POINTS_RENDER_EXPORT void points_camera_set_view_matrix(struct points_camera_t *camera, const double data[16]);
POINTS_RENDER_EXPORT void points_camera_get_perspective_matrix(struct points_camera_t *camera, double data[16]);
POINTS_RENDER_EXPORT void points_camera_set_perspective_matrix(struct points_camera_t *camera, const double data[16]);
POINTS_RENDER_EXPORT void points_camera_set_perspective(struct points_camera_t *camera, double fov, double width, double height, double near, double far);
POINTS_RENDER_EXPORT void points_camera_perspective_properties(struct points_camera_t *camera, double *fov, double *aspect,
                                                        double *near, double *far);
POINTS_RENDER_EXPORT void points_camera_get_eye(struct points_camera_t *camera, double eye[3]);
POINTS_RENDER_EXPORT void points_camera_get_forward(struct points_camera_t *camera, double forward[3]);

struct points_arcball_t;
POINTS_RENDER_EXPORT struct points_arcball_t *points_arcball_create(struct points_camera_t *camera, const double center[3]);
POINTS_RENDER_EXPORT void points_arcball_destroy(struct points_arcball_t *arcball);
POINTS_RENDER_EXPORT void points_arcball_reset(struct points_arcball_t *arcball);
POINTS_RENDER_EXPORT void points_arcball_detect_upside_down(struct points_arcball_t *arcball);
POINTS_RENDER_EXPORT void points_arcball_rotate(struct points_arcball_t *arcball, float normalized_dx, float normalized_dy, float normalized_dz);
POINTS_RENDER_EXPORT void points_arcball_pan(struct points_arcball_t *arcball, float normalized_dx, float normalized_dy);
POINTS_RENDER_EXPORT void points_arcball_pan_ground(struct points_arcball_t *arcball, float normalized_dx, float normalized_dy);
POINTS_RENDER_EXPORT void points_arcball_dolly(struct points_arcball_t *arcball, float normalized_dz);
POINTS_RENDER_EXPORT void points_arcball_zoom(struct points_arcball_t *arcball, float normalized_zoom);
POINTS_RENDER_EXPORT void points_arcball_set_up_axis(struct points_arcball_t *arcball, const double up[3]);
POINTS_RENDER_EXPORT void points_arcball_get_up_axis(struct points_arcball_t *arcball, double up[3]);
POINTS_RENDER_EXPORT void points_arcball_get_center(struct points_arcball_t *arcball, double center[3]);

struct points_fps_t;
POINTS_RENDER_EXPORT struct points_fps_t *points_fps_create(struct points_camera_t *camera);
POINTS_RENDER_EXPORT void points_fps_destroy(struct points_fps_t *fps);
POINTS_RENDER_EXPORT void points_fps_reset(struct points_fps_t *fps);
POINTS_RENDER_EXPORT void points_fps_rotate(struct points_fps_t *fps, float normalized_dx, float normalized_dy, float normalized_dz);
POINTS_RENDER_EXPORT void points_fps_move(struct points_fps_t *fps, float dx, float dy, float dz);

#ifdef __cplusplus
}
#endif
#endif // POINTS_CAMERA_H
