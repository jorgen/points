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
  POINTS_RENDER_EXPORT struct camera *camera_create();
  POINTS_RENDER_EXPORT void camera_destroy(struct camera *camera);
  POINTS_RENDER_EXPORT void camera_look_at(struct camera *camera, const double eye[3], const double center[3],
                                           const double up[3]);
  POINTS_RENDER_EXPORT void camera_look_at_aabb(struct camera *camera, struct aabb *aabb, const double direction[3],
                                                const double up[3]);
  POINTS_RENDER_EXPORT void camera_get_view_matrix(struct camera *camera, double data[16]);
  POINTS_RENDER_EXPORT void camera_set_view_matrix(struct camera *camera, const double data[16]);
  POINTS_RENDER_EXPORT void camera_get_perspective_matrix(struct camera *camera, double data[16]);
  POINTS_RENDER_EXPORT void camera_set_perspective_matrix(struct camera *camera, const double data[16]);
  POINTS_RENDER_EXPORT void camera_set_perspective(struct camera *camera, double fov, double width, double height,
                                                   double near, double far);
  POINTS_RENDER_EXPORT void camera_perspective_properties(struct camera *camera, double *fov, double *aspect,
                                                          double *near, double *far);

  namespace camera_manipulator
  {
  struct arcball;
  POINTS_RENDER_EXPORT struct arcball *arcball_create(struct camera *camera, const double center[3]);
  POINTS_RENDER_EXPORT void arcball_destroy(struct arcball *arcball);
  POINTS_RENDER_EXPORT void arcball_reset(struct arcball *arcball);
  POINTS_RENDER_EXPORT void arcball_rotate(struct arcball *arcball, float normalized_dx, float normalized_dy, float normalized_dz);
  POINTS_RENDER_EXPORT void arcball_zoom(struct arcball *arcball, float normalized_zoom);
  } // namespace camera_manipulator
  } // namespace render
  } // namespace points

#ifdef __cplusplus
}
#endif
#endif // POINTS_CAMERA_H