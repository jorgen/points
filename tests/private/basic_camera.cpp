#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <points/render/renderer.h>
#include <points/render/camera.h>
#include <points/render/aabb.h>
#include <camera.hpp>
#include <frustum.hpp>
#include <aabb.hpp>
#include <glm_include.hpp>

TEST_CASE("Verify_Perspective", "[render]")
{
  points::render::camera_t* camera = points::render::camera_create();
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
  points::render::camera_t* camera = points::render::camera_create();
  points::render::camera_set_perspective(camera, points::render::to_radians(45.0), 10.0, 9.0, 0.01, 50.0);
  points::render::aabb_t aabb;
  aabb.min[0] = 0.5; aabb.min[1] = 0.25; aabb.min[2] = 0.34;
  aabb.max[0] = 1.1; aabb.max[1] = 0.56; aabb.max[2] = 0.45;
  glm::dvec3 center = points::render::aabb_center(aabb);
  center = glm::normalize(center);
  glm::dvec3 up(0.0, 1.0, 0.0);
  points::render::camera_look_at_aabb(camera, &aabb, glm::value_ptr(center), glm::value_ptr(up));

  auto frustum = points::render::make_frustum(camera->projection * camera->view);
  REQUIRE(points::render::frustum_contains_aabb(frustum, aabb));

  glm::dvec3 pos(camera->view[3]);
  glm::dmat4 origin_view = glm::translate(camera->view, pos * -1.0);
  glm::dmat4 rotated10 = glm::rotate(origin_view, points::render::to_radians(10.0), glm::dvec3(0.0, 1.0, 0.0));
  glm::dmat4 rotated10View = glm::translate(rotated10, pos);

  auto rotated_frustum10 = points::render::make_frustum(camera->projection * rotated10View);
  REQUIRE(points::render::frustum_contains_aabb(rotated_frustum10, aabb));
  
  glm::dmat4 rotated80 = glm::rotate(origin_view, points::render::to_radians(130.0), glm::dvec3(0.0, 1.0, 0.0));
  glm::dmat4 rotated80View = glm::translate(rotated80, pos);
  auto rotated_frustum80 = points::render::make_frustum(camera->projection * rotated80View);

  REQUIRE(points::render::frustum_contains_aabb2(camera->projection * camera->view, aabb));
  REQUIRE(points::render::frustum_contains_aabb2(camera->projection * rotated10View, aabb));
  REQUIRE(!points::render::frustum_contains_aabb2(camera->projection * rotated80View, aabb));
  REQUIRE(!points::render::frustum_contains_aabb(rotated_frustum80, aabb));
}
