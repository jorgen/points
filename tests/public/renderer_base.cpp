#include <doctest/doctest.h>
#include <fmt/printf.h>
#include <dew/render/renderer.h>
#include <dew/render/camera.h>

TEST_CASE("CreateRenderer")
{
  auto* renderer = dew_renderer_create();
  auto* camera = dew_camera_create();
  auto frame = dew_renderer_frame(renderer, camera);
  dew_camera_destroy(camera);
  dew_renderer_destroy(renderer);
}
