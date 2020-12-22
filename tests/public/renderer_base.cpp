#include <catch2/catch.hpp>
#include <fmt/printf.h>
#include <points/render/renderer.h>
#include <points/render/camera.h>

TEST_CASE("CreateRenderer", "[render]")
{
  auto* renderer = points::render::renderer_create();
  auto* camera = points::render::camera_create();
  auto frame = points::render::renderer_frame(renderer, camera);

}
