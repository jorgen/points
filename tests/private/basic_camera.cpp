#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <points/render/renderer.h>
#include <points/render/camera.h>
#include <camera_p.h>
#include <glm_include.h>

TEST_CASE("Verify_Perspective", "[render]")
{
  points::render::camera* camera = points::render::camera_create();
  glm::dmat4 perspective = glm::perspectiveFov(90.0, 1024.0, 768.0, 3.3, 2345.0);
  points::render::camera_set_perspective_matrix(camera, glm::value_ptr(perspective));

  REQUIRE(memcmp(&perspective, &camera->perspective, sizeof(perspective)) == 0);

  camera->updateInverseProjectionProperties();
  REQUIRE(camera->fov == 90.0);
  REQUIRE(camera->near == 3.3);
  REQUIRE(camera->far == 2345.0);
  REQUIRE(camera->aspect == 1024.0 / 768.0);
}
