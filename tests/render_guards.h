#pragma once
#include <memory>
#include <camera.hpp>
#include <points/render/camera.h>
#include <points/render/renderer.h>

namespace points::render
{
struct renderer_deleter
{
  void operator()(points_renderer_t *r) const { points_renderer_destroy(r); }
};
using unique_renderer = std::unique_ptr<points_renderer_t, renderer_deleter>;

struct camera_deleter
{
  void operator()(points_camera_t *c) const { points_camera_destroy(c); }
};
using unique_camera = std::unique_ptr<points_camera_t, camera_deleter>;

namespace camera_manipulator
{
struct arcball_deleter
{
  void operator()(points_arcball_t *a) const { points_arcball_destroy(a); }
};
using unique_arcball = std::unique_ptr<points_arcball_t, arcball_deleter>;
} // namespace camera_manipulator
} // namespace points::render
