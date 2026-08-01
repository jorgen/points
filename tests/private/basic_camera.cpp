#include <aabb.hpp>
#include <camera.hpp>
#include <doctest/doctest.h>
#include <fmt/printf.h>
#include <frustum.hpp>
#include <glm_include.hpp>
#include <dew/render/aabb.h>
#include <dew/render/camera.h>
#include <dew/render/renderer.h>
#include "render_guards.h"

TEST_CASE("Verify_Perspective")
{
  dew::render::unique_camera camera(dew_camera_create());
  glm::dmat4 perspective = glm::perspectiveFov(dew::render::to_radians(90.0), 1024.0, 768.0, 3.3, 2345.0);
  dew_camera_set_perspective_matrix(camera.get(), glm::value_ptr(perspective));

  REQUIRE(memcmp(&perspective, &camera->projection, sizeof(perspective)) == 0);

  double fov;
  double aspect;
  double near;
  double far;
  dew_camera_perspective_properties(camera.get(), &fov, &aspect, &near, &far);

  REQUIRE(near == doctest::Approx(3.3));
  REQUIRE(far == doctest::Approx(2345.0));
  REQUIRE(fov == doctest::Approx(dew::render::to_radians(90.0)));
  REQUIRE(aspect == doctest::Approx(1024.0 / 768.0));
}

TEST_CASE("aabb_center returns the geometric midpoint")
{
  dew_aabb_t aabb;
  aabb.min[0] = 10.0;
  aabb.min[1] = 0.0;
  aabb.min[2] = 0.0;
  aabb.max[0] = 12.0;
  aabb.max[1] = 4.0;
  aabb.max[2] = 6.0;
  glm::dvec3 center = dew::render::aabb_center(aabb);
  REQUIRE(center.x == doctest::Approx(11.0));
  REQUIRE(center.y == doctest::Approx(2.0));
  REQUIRE(center.z == doctest::Approx(3.0));
}

TEST_CASE("Check_Frustum_AABB_Culling")
{
  dew::render::unique_camera camera(dew_camera_create());
  dew_camera_set_perspective(camera.get(), dew::render::to_radians(45.0), 10.0, 9.0, 0.01, 50.0);
  dew_aabb_t aabb;
  aabb.min[0] = 0.5;
  aabb.min[1] = 0.25;
  aabb.min[2] = 0.34;
  aabb.max[0] = 1.1;
  aabb.max[1] = 0.56;
  aabb.max[2] = 0.45;
  glm::dvec3 aabb_min = glm::dvec3(aabb.min[0], aabb.min[1], aabb.min[2]);
  glm::dvec3 aabb_max = glm::dvec3(aabb.max[0], aabb.max[1], aabb.max[2]);
  glm::dvec3 center = dew::render::aabb_center(aabb);
  center = glm::normalize(center);
  glm::dvec3 up(0.0, 1.0, 0.0);
  dew_camera_look_at_aabb(camera.get(), &aabb, glm::value_ptr(center), glm::value_ptr(up));

  dew::render::frustum_t frustum{};
  frustum.update(camera->projection * camera->view);
  REQUIRE(frustum.test_aabb(aabb_min, aabb_max) == dew::render::frustum_intersection_t::inside);

  glm::dmat4 rot25 = glm::rotate(glm::dmat4(1.0), dew::render::to_radians(25.0), glm::dvec3(0.0, 1.0, 0.0));
  frustum.update(camera->projection * rot25 * camera->view);
  REQUIRE(frustum.test_aabb(aabb_min, aabb_max) == dew::render::frustum_intersection_t::intersects);

  glm::dmat4 rot50 = glm::rotate(glm::dmat4(1.0), dew::render::to_radians(50.0), glm::dvec3(0.0, 1.0, 0.0));
  frustum.update(camera->projection * rot50 * camera->view);
  REQUIRE(frustum.test_aabb(aabb_min, aabb_max) == dew::render::frustum_intersection_t::outside);
}
