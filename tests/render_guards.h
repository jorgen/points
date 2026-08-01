#pragma once
#include <memory>
#include <camera.hpp>
#include <dew/render/camera.h>
#include <dew/render/renderer.h>

namespace dew::render
{
struct renderer_deleter
{
  void operator()(dew_renderer_t *r) const { dew_renderer_destroy(r); }
};
using unique_renderer = std::unique_ptr<dew_renderer_t, renderer_deleter>;

struct camera_deleter
{
  void operator()(dew_camera_t *c) const { dew_camera_destroy(c); }
};
using unique_camera = std::unique_ptr<dew_camera_t, camera_deleter>;

namespace camera_manipulator
{
struct arcball_deleter
{
  void operator()(dew_arcball_t *a) const { dew_arcball_destroy(a); }
};
using unique_arcball = std::unique_ptr<dew_arcball_t, arcball_deleter>;
} // namespace camera_manipulator
} // namespace dew::render
