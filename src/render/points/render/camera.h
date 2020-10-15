#ifndef POINTS_CAMERA_H
#define POINTS_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <points/render/points_render_export.h>

namespace points
{
  namespace render
  {
    struct camera;
    POINTS_RENDER_EXPORT struct camera* camera_create();
    POINTS_RENDER_EXPORT void camera_destroy(struct camera* camera);
    POINTS_RENDER_EXPORT void camera_look_at(struct camera *camera, float* eye, float* center, float* up);
    POINTS_RENDER_EXPORT void camera_set_view_matrix(struct camera* camera, float* data);
    POINTS_RENDER_EXPORT void camera_set_perspective_matrix(struct camera *camera, float* data);
    POINTS_RENDER_EXPORT void camera_set_perspective(struct camera *camera, float fov, float width, float height, float near, float far);
  }
}

#ifdef __cplusplus
}
#endif
#endif //POINTS_CAMERA_H