#ifndef POINTS_CAMERA_H
#define POINTS_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <points/render/export.h>

namespace points
{
  namespace render
  {
    struct camera;
    POINTS_RENDER_EXPORT struct camera* camera_create();
    POINTS_RENDER_EXPORT void camera_destroy(struct camera* camera);
    POINTS_RENDER_EXPORT void camera_look_at(struct camera *camera, double* eye, double* center, double* up);
    POINTS_RENDER_EXPORT void camera_look_at_aabb(struct camera *camera, struct aabb *aabb, double *direction, double *up);
    POINTS_RENDER_EXPORT void camera_set_view_matrix(struct camera* camera, double* data);
    POINTS_RENDER_EXPORT void camera_set_perspective_matrix(struct camera *camera, double* data);
    POINTS_RENDER_EXPORT void camera_set_perspective(struct camera *camera, double fov, double width, double height, double near, double far);

  }
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_CAMERA_H