#include <doctest/doctest.h>
#include "approx_abs.h"
#include <camera.hpp>
#include <points/render/camera.h>

#include <cmath>

using namespace points::render;
using namespace points::render::camera_manipulator;

static constexpr double MARGIN = 1e-6;

TEST_CASE("arcball create initial state")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  REQUIRE(arc->distance == approx_abs(10.0, MARGIN));

  double arc_center[3];
  arcball_get_center(arc, arc_center);
  REQUIRE(arc_center[0] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_center[1] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_center[2] == approx_abs(0.0, MARGIN));

  double arc_up[3];
  arcball_get_up_axis(arc, arc_up);
  REQUIRE(arc_up[0] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_up[1] == approx_abs(1.0, MARGIN));
  REQUIRE(arc_up[2] == approx_abs(0.0, MARGIN));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball rotate yaw")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);
  double initial_yaw = arc->yaw;

  float dx = 0.25f;
  arcball_rotate(arc, dx, 0.0f, 0.0f);

  REQUIRE(arc->yaw == approx_abs(initial_yaw + dx * M_PI, MARGIN));
  REQUIRE(arc->pitch == approx_abs(0.0, 0.01));

  // Eye should have moved laterally
  double cam_eye[3];
  camera_get_eye(cam, cam_eye);
  // After yaw rotation from (0,0,10), eye should no longer be on Z axis
  REQUIRE(std::abs(cam_eye[0]) > 0.1);

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball rotate pitch clamped")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  // Very large pitch rotation — should be clamped to ±89°
  arcball_rotate(arc, 0.0f, 10.0f, 0.0f);

  constexpr double max_pitch = 89.0 * M_PI / 180.0;
  REQUIRE(arc->pitch == approx_abs(max_pitch, MARGIN));

  // Try negative
  arcball_rotate(arc, 0.0f, -20.0f, 0.0f);
  REQUIRE(arc->pitch == approx_abs(-max_pitch, MARGIN));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball pan moves center")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  double center_before[3];
  arcball_get_center(arc, center_before);

  arcball_pan(arc, 0.1f, 0.2f);

  double center_after[3];
  arcball_get_center(arc, center_after);

  // Center should have moved
  double dx = center_after[0] - center_before[0];
  double dy = center_after[1] - center_before[1];
  REQUIRE((std::abs(dx) > 0.01 || std::abs(dy) > 0.01));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball dolly moves center forward")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  double center_before[3];
  arcball_get_center(arc, center_before);

  arcball_dolly(arc, 0.1f);

  double center_after[3];
  arcball_get_center(arc, center_after);

  // Center should have shifted along the forward direction
  double dz = center_after[2] - center_before[2];
  REQUIRE(std::abs(dz) > 0.01);

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball zoom changes distance")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);
  double initial_distance = arc->distance;

  // Positive zoom (zoom out)
  arcball_zoom(arc, 0.5f);
  REQUIRE(arc->distance == approx_abs(initial_distance * 1.5, MARGIN));

  // Negative zoom (zoom in)
  double current = arc->distance;
  arcball_zoom(arc, -0.3f);
  REQUIRE(arc->distance == approx_abs(current * 0.7, MARGIN));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball zoom clamps minimum")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 0.02};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  // Zoom in extremely
  arcball_zoom(arc, -0.999f);

  REQUIRE(arc->distance >= 0.01 - MARGIN);

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball set up axis Z-up")
{
  auto *cam = camera_create();
  double eye[] = {0, 10, 0};
  double center[] = {0, 0, 0};
  double up[] = {0, 0, 1};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  double new_up[] = {0, 0, 1};
  arcball_set_up_axis(arc, new_up);

  double arc_up[3];
  arcball_get_up_axis(arc, arc_up);
  REQUIRE(arc_up[0] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_up[1] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_up[2] == approx_abs(1.0, MARGIN));

  // View matrix should have been recomputed — verify by checking eye position
  double cam_eye[3];
  camera_get_eye(cam, cam_eye);
  double dist = std::sqrt(cam_eye[0] * cam_eye[0] + cam_eye[1] * cam_eye[1] + cam_eye[2] * cam_eye[2]);
  REQUIRE(dist == approx_abs(arc->distance, 0.01));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball reset re-derives angles")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  // Rotate to a new orientation
  arcball_rotate(arc, 0.3f, 0.2f, 0.0f);

  // Capture current view
  double view_before[16];
  camera_get_view_matrix(cam, view_before);

  // Reset re-derives from current view
  arcball_reset(arc);

  double view_after[16];
  camera_get_view_matrix(cam, view_after);

  // View should be essentially the same after reset
  for (int i = 0; i < 16; i++)
    REQUIRE(view_after[i] == approx_abs(view_before[i], 1e-4));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("arcball get center and up axis")
{
  auto *cam = camera_create();
  double eye[] = {5, 3, 2};
  double center[] = {1, 2, 3};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  auto *arc = arcball_create(cam, center);

  double arc_center[3];
  arcball_get_center(arc, arc_center);
  REQUIRE(arc_center[0] == approx_abs(1.0, MARGIN));
  REQUIRE(arc_center[1] == approx_abs(2.0, MARGIN));
  REQUIRE(arc_center[2] == approx_abs(3.0, MARGIN));

  double arc_up[3];
  arcball_get_up_axis(arc, arc_up);
  REQUIRE(arc_up[0] == approx_abs(0.0, MARGIN));
  REQUIRE(arc_up[1] == approx_abs(1.0, MARGIN));
  REQUIRE(arc_up[2] == approx_abs(0.0, MARGIN));

  arcball_destroy(arc);
  camera_destroy(cam);
}

TEST_CASE("camera_get_eye")
{
  auto *cam = camera_create();
  double eye_in[] = {5, 0, 0};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye_in, center, up);

  double eye_out[3];
  camera_get_eye(cam, eye_out);
  REQUIRE(eye_out[0] == approx_abs(5.0, MARGIN));
  REQUIRE(eye_out[1] == approx_abs(0.0, MARGIN));
  REQUIRE(eye_out[2] == approx_abs(0.0, MARGIN));

  camera_destroy(cam);
}

TEST_CASE("camera_get_forward")
{
  auto *cam = camera_create();
  double eye[] = {0, 0, 10};
  double center[] = {0, 0, 0};
  double up[] = {0, 1, 0};
  camera_look_at(cam, eye, center, up);

  double forward[3];
  camera_get_forward(cam, forward);
  REQUIRE(forward[0] == approx_abs(0.0, MARGIN));
  REQUIRE(forward[1] == approx_abs(0.0, MARGIN));
  REQUIRE(forward[2] == approx_abs(-1.0, MARGIN));

  camera_destroy(cam);
}
