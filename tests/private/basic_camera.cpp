#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <points/render/renderer.h>
#include <points/render/camera.h>
#include <points/render/aabb.h>
#include <camera_p.h>
#include <frustum_p.h>
#include <glm_include.h>

TEST_CASE("Verify_Perspective", "[render]")
{
  points::render::camera* camera = points::render::camera_create();
  glm::dmat4 perspective = glm::perspectiveFov(points::render::to_radians(90.0), 1024.0, 768.0, 3.3, 2345.0);
  points::render::camera_set_perspective_matrix(camera, glm::value_ptr(perspective));

  REQUIRE(memcmp(&perspective, &camera->projection, sizeof(perspective)) == 0);

  double fov;
  double aspect;
  double near;
  double far;
  points::render::camera_perspective_properties(camera, &fov, &aspect, &near, &far);

  REQUIRE(near == Approx(3.3));
  REQUIRE(far == Approx(2345.0));
  REQUIRE(fov == Approx(points::render::to_radians(90.0)));
  REQUIRE(aspect == Approx(1024.0 / 768.0));
}

TEST_CASE("Check_Frustum_AABB_Culling", "[render]")
{
  points::render::camera* camera = points::render::camera_create();
  points::render::camera_set_perspective(camera, points::render::to_radians(45.0), 10.0, 9.0, 0.01, 1000.0);
  points::render::aabb aabb;
  aabb.min[0] = 0.5; aabb.min[1] = 0.25; aabb.min[2] = 0.34;
  aabb.max[0] = 1.1; aabb.max[1] = 0.56; aabb.max[2] = 0.45;
  glm::dvec3 center(0.0, 0.0, 0.0);
  glm::dvec3 up(0.0, 1.0, 0.0);
  points::render::camera_look_at_aabb(camera, &aabb, glm::value_ptr(center), glm::value_ptr(up));

  auto frustum = points::render::make_frustum(camera->view * camera->projection);
  REQUIRE(points::render::frustum_contains_aabb(frustum, aabb));

  auto rotated_frustum10 = points::render::make_frustum(glm::rotate(glm::dmat4(), points::render::to_radians(10.0), glm::dvec3(1.0, 0.0, 0.0)) * camera->view * camera->projection);
  REQUIRE(points::render::frustum_contains_aabb(frustum, aabb));
  
  auto rotated_frustum50 = points::render::make_frustum(glm::rotate(glm::dmat4(), points::render::to_radians(30.0), glm::dvec3(0.0, 1.0, 0.0)) * camera->view * camera->projection);
  REQUIRE(!points::render::frustum_contains_aabb(frustum, aabb));
}
