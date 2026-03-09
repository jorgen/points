#include <doctest/doctest.h>
#include <fmt/printf.h>
#include <points/render/renderer.h>
#include <points/render/camera.h>

TEST_CASE("CreateRenderer")
{
  auto* renderer = points_renderer_create();
  auto* camera = points_camera_create();
  auto frame = points_renderer_frame(renderer, camera);
  points_camera_destroy(camera);
  points_renderer_destroy(renderer);
}
