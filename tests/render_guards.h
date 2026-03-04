#pragma once
#include <memory>
#include <camera.hpp>
#include <points/render/camera.h>

namespace points::render
{
struct camera_deleter
{
  void operator()(camera_t *c) const { camera_destroy(c); }
};
using unique_camera = std::unique_ptr<camera_t, camera_deleter>;

namespace camera_manipulator
{
struct arcball_deleter
{
  void operator()(arcball_t *a) const { arcball_destroy(a); }
};
using unique_arcball = std::unique_ptr<arcball_t, arcball_deleter>;
} // namespace camera_manipulator
} // namespace points::render
