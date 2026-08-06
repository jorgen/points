/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2026  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Affero General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU Affero General Public License for more details.
**
** You should have received a copy of the GNU Affero General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/

// The renderer, driven with a recording consumer instead of OpenGL.
//
// Until now the render path had no automated test at all, and "it compiles" was the only signal --
// which is why every change to it has had to be checked by eye, and why moving the streaming source
// off the write pipeline has stayed parked. Nothing about the renderer actually needs a GPU, though:
// it is callback-driven by design and hands the consumer buffer lifecycle events plus a list of draw
// groups. A consumer that records instead of drawing is a complete, honest test harness.
//
// What these pin down:
//
//   * a camera pointed at data eventually produces point draw groups, and every buffer they name was
//     created and initialized first -- a draw group referencing an unknown handle is what a real
//     consumer would crash on;
//   * the buffer lifecycle balances. Every created buffer is destroyed by teardown; a leak here is a
//     GPU leak in the real consumer, invisible until a session runs long;
//   * frustum culling is real: pointing the camera away drops the point groups;
//   * moving the camera closer refines rather than merely redrawing -- more points get uploaded.
//
// The dataset is synthetic and built by the test. It goes to a temp .dew file rather than mem://,
// which would have been the natural choice: vio's create_io_manager makes a FRESH memory_io_manager_t
// per call and ignores the name, so a converter and a reader on the same mem:// URL get two different
// stores. Worth knowing before reaching for it again.

#include <doctest/doctest.h>

#include <dew/converter/converter.h>
#include <dew/converter/converter_data_source.h>
#include <dew/core/default_attribute_names.h>
#include <dew/core/error.h>
#include <dew/render/camera.h>
#include <dew/render/renderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------------------------
// A synthetic cloud: a filled cube of points, dense enough to force real subdivision.
constexpr uint32_t k_side = 40;
constexpr uint32_t k_point_count = k_side * k_side * k_side; // 64000
constexpr double k_scale = 0.01;

struct source_t
{
  std::vector<int32_t> xyz;
  std::vector<uint16_t> intensity;
};

source_t g_source;
uint32_t g_emitted = 0;

source_t make_source()
{
  source_t out;
  out.xyz.reserve(size_t(k_point_count) * 3);
  out.intensity.reserve(k_point_count);
  for (uint32_t z = 0; z < k_side; z++)
    for (uint32_t y = 0; y < k_side; y++)
      for (uint32_t x = 0; x < k_side; x++)
      {
        out.xyz.push_back(int32_t(x));
        out.xyz.push_back(int32_t(y));
        out.xyz.push_back(int32_t(z));
        out.intensity.push_back(uint16_t(x + y + z));
      }
  return out;
}

dew_converter_file_pre_init_info_t pre_init(const char *, size_t, dew_error_t **)
{
  dew_converter_file_pre_init_info_t info{};
  info.approximate_point_count = k_point_count;
  info.found_point_count = 1;
  info.approximate_point_size_bytes = 16;
  info.scale[0] = info.scale[1] = info.scale[2] = k_scale;
  info.found_scale = 1;
  return info;
}

void init(const char *, size_t, dew_converter_header_t *header, dew_attributes_t *attributes, void **, dew_error_t **)
{
  header->point_count = k_point_count;
  for (int i = 0; i < 3; i++)
  {
    header->offset[i] = 0.0;
    header->scale[i] = k_scale;
    header->min[i] = 0.0;
    header->max[i] = double(k_side - 1) * k_scale;
  }
  dew_attributes_add_attribute(attributes, DEW_ATTRIBUTE_XYZ, uint32_t(strlen(DEW_ATTRIBUTE_XYZ)), dew_type_i32, dew_components_3);
  dew_attributes_add_attribute(attributes, DEW_ATTRIBUTE_INTENSITY, uint32_t(strlen(DEW_ATTRIBUTE_INTENSITY)), dew_type_u16, dew_components_1);
  g_emitted = 0;
}

void convert_data(void *, const dew_converter_header_t *, const dew_attribute_t *, uint32_t, uint32_t max_points, dew_blob_t *buffers, uint32_t buffer_count, uint32_t *points_read, uint8_t *done, dew_error_t **)
{
  const uint32_t remaining = k_point_count - g_emitted;
  const uint32_t n = remaining < max_points ? remaining : max_points;
  if (buffer_count >= 1)
    memcpy(buffers[0].data, g_source.xyz.data() + size_t(g_emitted) * 3, size_t(n) * 3 * sizeof(int32_t));
  if (buffer_count >= 2)
    memcpy(buffers[1].data, g_source.intensity.data() + g_emitted, size_t(n) * sizeof(uint16_t));
  g_emitted += n;
  *points_read = n;
  *done = g_emitted >= k_point_count ? 1 : 0;
}

bool build_dataset(const char *path)
{
  g_source = make_source();
  std::remove(path);
  dew_error_t *error = nullptr;
  auto *converter = dew_converter_create(path, uint32_t(strlen(path)), dew_open_file_semantics_truncate, &error);
  if (!converter)
  {
    if (error)
      dew_error_destroy(error);
    return false;
  }
  dew_converter_file_convert_callbacks_t callbacks{};
  callbacks.pre_init = pre_init;
  callbacks.init = init;
  callbacks.convert_data = convert_data;
  dew_converter_set_file_converter_callbacks(converter, callbacks);
  // Force a real octree. With the default limit the whole cube lands in one node and there is no LOD
  // structure for the walker to descend, which would make every assertion below trivially true.
  dew_converter_set_node_point_limit(converter, 2000);

  dew_converter_str_buffer name{"synthetic", 9};
  dew_converter_add_data_file(converter, &name, 1);
  dew_converter_wait_idle(converter);
  const bool ok = dew_converter_status(converter) == dew_conversion_status_completed;
  dew_converter_destroy(converter);
  return ok;
}

// ---------------------------------------------------------------------------------------------
// The consumer under test: records instead of drawing.
struct recording_consumer_t
{
  struct buffer_record_t
  {
    dew_buffer_type_t type = dew_buffer_type_vertex;
    int initialized_bytes = 0;
    int initialize_calls = 0;
    int modify_calls = 0;
    bool destroyed = false;
  };

  // Handles are dense small integers rather than pointers, so a stale handle shows up as a lookup
  // miss instead of a use-after-free that happens to still be mapped.
  std::unordered_map<uintptr_t, buffer_record_t> buffers;
  uintptr_t next_handle = 1;

  uint32_t create_calls = 0;
  uint32_t initialize_calls = 0;
  uint32_t modify_calls = 0;
  uint32_t destroy_calls = 0;
  uint64_t initialized_bytes = 0;

  [[nodiscard]] size_t live_buffers() const
  {
    size_t live = 0;
    for (const auto &entry : buffers)
      if (!entry.second.destroyed)
        live++;
    return live;
  }

  [[nodiscard]] bool known(void *user_ptr) const
  {
    auto it = buffers.find(reinterpret_cast<uintptr_t>(user_ptr));
    return it != buffers.end() && !it->second.destroyed;
  }

  [[nodiscard]] bool initialized(void *user_ptr) const
  {
    auto it = buffers.find(reinterpret_cast<uintptr_t>(user_ptr));
    return it != buffers.end() && it->second.initialize_calls > 0;
  }

  static recording_consumer_t *self(void *user_ptr) { return static_cast<recording_consumer_t *>(user_ptr); }

  static void on_create(dew_renderer_t *, void *user_ptr, dew_buffer_type_t type, void **buffer_user_ptr)
  {
    auto *consumer = self(user_ptr);
    const uintptr_t handle = consumer->next_handle++;
    consumer->buffers[handle] = buffer_record_t{type, 0, 0, 0, false};
    consumer->create_calls++;
    *buffer_user_ptr = reinterpret_cast<void *>(handle);
  }

  static void on_initialize(dew_renderer_t *, void *user_ptr, dew_buffer_t *, void *buffer_user_ptr, dew_type_t, dew_components_t, int buffer_size, void *)
  {
    auto *consumer = self(user_ptr);
    auto it = consumer->buffers.find(reinterpret_cast<uintptr_t>(buffer_user_ptr));
    REQUIRE(it != consumer->buffers.end()); // initialize before create would be a lifecycle bug
    it->second.initialized_bytes = buffer_size;
    it->second.initialize_calls++;
    consumer->initialize_calls++;
    consumer->initialized_bytes += uint64_t(buffer_size < 0 ? 0 : buffer_size);
  }

  static void on_modify(dew_renderer_t *, void *user_ptr, dew_buffer_t *, void *buffer_user_ptr, int, int, void *)
  {
    auto *consumer = self(user_ptr);
    auto it = consumer->buffers.find(reinterpret_cast<uintptr_t>(buffer_user_ptr));
    REQUIRE(it != consumer->buffers.end());
    it->second.modify_calls++;
    consumer->modify_calls++;
  }

  static void on_destroy(dew_renderer_t *, void *user_ptr, void *buffer_user_ptr)
  {
    auto *consumer = self(user_ptr);
    auto it = consumer->buffers.find(reinterpret_cast<uintptr_t>(buffer_user_ptr));
    REQUIRE(it != consumer->buffers.end()); // destroying something never created
    REQUIRE(!it->second.destroyed);         // double destroy
    it->second.destroyed = true;
    consumer->destroy_calls++;
  }

  dew_renderer_callbacks_t callbacks()
  {
    dew_renderer_callbacks_t out{};
    out.create_buffer = &recording_consumer_t::on_create;
    out.initialize_buffer = &recording_consumer_t::on_initialize;
    out.modify_buffer = &recording_consumer_t::on_modify;
    out.destroy_buffer = &recording_consumer_t::on_destroy;
    return out;
  }
};

// What one frame produced, flattened for assertions.
struct frame_summary_t
{
  int group_count = 0;
  int point_group_count = 0;
  uint64_t point_draw_size = 0;
  std::vector<dew_draw_type_t> types;
};

bool is_point_group(dew_draw_type_t type)
{
  return type == dew_dyn_points_1 || type == dew_dyn_points_3 || type == dew_dyn_points_crossfade || type == dew_flat_points;
}

frame_summary_t run_frame(dew_renderer_t *renderer, dew_camera_t *camera, recording_consumer_t &consumer)
{
  frame_summary_t summary;
  dew_frame_t frame = dew_renderer_frame(renderer, camera);
  summary.group_count = frame.to_render_size;
  for (int i = 0; i < frame.to_render_size; i++)
  {
    const auto &group = frame.to_render[i];
    summary.types.push_back(group.draw_type);
    // The contract a real consumer depends on: every handle in a draw group is one the renderer
    // asked us to create and then initialize. A group naming an unknown or destroyed handle is
    // precisely what would crash a GL backend.
    for (int b = 0; b < group.buffers_size; b++)
    {
      if (!group.buffers[b].user_ptr)
        continue;
      REQUIRE(consumer.known(group.buffers[b].user_ptr));
      REQUIRE(consumer.initialized(group.buffers[b].user_ptr));
    }
    if (is_point_group(group.draw_type) && group.draw_size > 0)
    {
      summary.point_group_count++;
      summary.point_draw_size += uint64_t(group.draw_size);
    }
  }
  return summary;
}

// Streaming is asynchronous: the first frames walk the tree and issue loads, and points appear over
// the following ones. So "render until it settles" is the only honest way to observe a steady state.
frame_summary_t render_until_points(dew_renderer_t *renderer, dew_camera_t *camera, recording_consumer_t &consumer, int max_frames = 400)
{
  frame_summary_t last;
  for (int i = 0; i < max_frames; i++)
  {
    last = run_frame(renderer, camera, consumer);
    if (last.point_draw_size > 0 && i > 8)
      return last;
  }
  return last;
}

frame_summary_t render_frames(dew_renderer_t *renderer, dew_camera_t *camera, recording_consumer_t &consumer, int frames)
{
  frame_summary_t last;
  for (int i = 0; i < frames; i++)
    last = run_frame(renderer, camera, consumer);
  return last;
}

// Render until `predicate` holds, or give up.
//
// Everything about this pipeline is asynchronous -- reads land on the storage loop, decodes on a
// worker, uploads on the next frame, and departed nodes fade out over several more. Asserting after a
// FIXED number of frames therefore passes or fails on timing, which is exactly what an earlier version
// of these tests did: same binary, two runs, two answers. Waiting on the condition instead makes the
// bound a timeout rather than a guess, and a genuine regression still fails because the predicate
// never comes true.
template <typename Predicate> frame_summary_t render_until(dew_renderer_t *renderer, dew_camera_t *camera, recording_consumer_t &consumer, Predicate predicate, int max_frames = 600)
{
  frame_summary_t last;
  for (int i = 0; i < max_frames; i++)
  {
    last = run_frame(renderer, camera, consumer);
    if (predicate(last))
      return last;
  }
  return last;
}

const char *k_path = "render_callback_test.dew";

// Everything a test needs, torn down in the order a real consumer must use: data source first (it
// releases its GPU buffers), then the renderer.
struct render_fixture_t
{
  render_fixture_t()
  {
    REQUIRE(build_dataset(k_path));
    renderer = dew_renderer_create();
    REQUIRE(renderer);
    dew_renderer_set_callback(renderer, consumer.callbacks(), &consumer);

    // dew_error_t is opaque, so the handle has to be allocated rather than declared.
    dew_error_t *error = dew_error_create();
    data_source = dew_converter_data_source_create(k_path, uint32_t(strlen(k_path)), error, renderer);
    REQUIRE(data_source);
    dew_error_destroy(error);

    dew_converter_data_source_set_viewport(data_source, 800, 600);
    // Deliberately does NOT set a rendered attribute: the data source is supposed to pick one, and
    // this dataset (xyz + intensity) is exactly the shape that used to defeat its selection.
    dew_renderer_add_data_source(renderer, dew_converter_data_source_get(data_source));

    camera = dew_camera_create();
    REQUIRE(camera);
    dew_camera_set_perspective(camera, 45, 800, 600, 0.01, 10000);

    dew_converter_data_source_get_tight_aabb(data_source, aabb_min, aabb_max);
    for (int i = 0; i < 3; i++)
      center[i] = (aabb_min[i] + aabb_max[i]) * 0.5;
    extent = std::max({aabb_max[0] - aabb_min[0], aabb_max[1] - aabb_min[1], aabb_max[2] - aabb_min[2]});
    if (extent <= 0)
      extent = 1.0;
  }

  ~render_fixture_t()
  {
    if (camera)
      dew_camera_destroy(camera);
    if (data_source)
      dew_converter_data_source_destroy(data_source);
    if (renderer)
      dew_renderer_destroy(renderer);
  }

  void look_from(double dx, double dy, double dz, double distance_factor)
  {
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double d = extent * distance_factor / (length > 0 ? length : 1.0);
    const double eye[3] = {center[0] + dx * d, center[1] + dy * d, center[2] + dz * d};
    const double up[3] = {0, 0, 1};
    dew_camera_look_at(camera, eye, center, up);
  }

  recording_consumer_t consumer;
  dew_renderer_t *renderer = nullptr;
  dew_converter_data_source_t *data_source = nullptr;
  dew_camera_t *camera = nullptr;
  double aabb_min[3] = {0, 0, 0};
  double aabb_max[3] = {0, 0, 0};
  double center[3] = {0, 0, 0};
  double extent = 1.0;
};

} // namespace

TEST_CASE("render: a camera on the data produces point draw groups over consistent buffers")
{
  render_fixture_t fixture;
  fixture.look_from(1, 1, 1, 2.0);

  auto summary = render_until_points(fixture.renderer, fixture.camera, fixture.consumer);

  // The headline: the renderer asked us to draw actual points. run_frame has already asserted that
  // every buffer handle in every group was created and initialized first.
  MESSAGE("point groups: ", summary.point_group_count, "  points drawn: ", summary.point_draw_size, "  buffers created: ", fixture.consumer.create_calls, "  uploaded bytes: ",
          fixture.consumer.initialized_bytes);
  REQUIRE(summary.point_group_count > 0);
  REQUIRE(summary.point_draw_size > 0);
  REQUIRE(fixture.consumer.create_calls > 0);
  REQUIRE(fixture.consumer.initialize_calls > 0);
  REQUIRE(fixture.consumer.initialized_bytes > 0);

  // Points are streamed per node, so a cube this size must arrive as more than one buffer -- a single
  // one would mean the octree was never subdivided and the walk is not doing its job.
  REQUIRE(fixture.consumer.create_calls > 1);
}

TEST_CASE("render: pointing the camera away drops the point groups")
{
  // Frustum culling, observed rather than assumed. If the walker ignored the frustum this would draw
  // exactly as much with the data behind the camera as in front of it.
  render_fixture_t fixture;
  fixture.look_from(1, 1, 1, 2.0);
  auto facing = render_until_points(fixture.renderer, fixture.camera, fixture.consumer);
  REQUIRE(facing.point_draw_size > 0);

  // Same position, looking the other way.
  const double d = fixture.extent * 2.0 / std::sqrt(3.0);
  const double eye[3] = {fixture.center[0] + d, fixture.center[1] + d, fixture.center[2] + d};
  const double away[3] = {eye[0] + d, eye[1] + d, eye[2] + d};
  const double up[3] = {0, 0, 1};
  dew_camera_look_at(fixture.camera, eye, away, up);

  // Wait for the fade-out rather than assuming a frame count: departed nodes are crossfaded, so the
  // count decays over several frames.
  auto behind = render_until(fixture.renderer, fixture.camera, fixture.consumer, [](const frame_summary_t &s) { return s.point_draw_size == 0; });
  MESSAGE("facing: ", facing.point_draw_size, "  looking away: ", behind.point_draw_size);
  REQUIRE(behind.point_draw_size == 0);
}

TEST_CASE("render: moving closer refines the cloud")
{
  // The streaming contract: a nearer camera means a finer LOD frontier, which means more uploaded
  // points -- not merely the same buffers redrawn.
  render_fixture_t fixture;
  fixture.look_from(1, 1, 1, 6.0);
  render_until_points(fixture.renderer, fixture.camera, fixture.consumer);
  // Let the far view reach a steady state: stop once a stretch of frames uploads nothing new.
  uint64_t settled = 0;
  int quiet = 0;
  auto far_away = render_until(fixture.renderer, fixture.camera, fixture.consumer, [&](const frame_summary_t &) {
    if (fixture.consumer.initialized_bytes == settled)
      return ++quiet >= 30;
    settled = fixture.consumer.initialized_bytes;
    quiet = 0;
    return false;
  });
  const uint64_t bytes_when_far = fixture.consumer.initialized_bytes;
  REQUIRE(far_away.point_draw_size > 0);

  fixture.look_from(1, 1, 1, 0.9);
  render_until(fixture.renderer, fixture.camera, fixture.consumer, [&](const frame_summary_t &) { return fixture.consumer.initialized_bytes > bytes_when_far; });

  // More data had to be uploaded to serve the closer view.
  MESSAGE("uploaded bytes far: ", bytes_when_far, "  after moving close: ", fixture.consumer.initialized_bytes);
  REQUIRE(fixture.consumer.initialized_bytes > bytes_when_far);
}

TEST_CASE("render: buffer lifecycle balances across a full session")
{
  render_fixture_t fixture;
  fixture.look_from(1, 1, 1, 1.5);
  render_until_points(fixture.renderer, fixture.camera, fixture.consumer);
  render_frames(fixture.renderer, fixture.camera, fixture.consumer, 40);

  REQUIRE(fixture.consumer.create_calls > 0);
  REQUIRE(fixture.consumer.live_buffers() > 0);

  // Tear the data source down explicitly, which is what releases its buffers, and then check nothing
  // it created is still outstanding.
  dew_converter_data_source_destroy(fixture.data_source);
  fixture.data_source = nullptr;

  MESSAGE("buffers created: ", fixture.consumer.create_calls, "  destroyed: ", fixture.consumer.destroy_calls);
  REQUIRE(fixture.consumer.destroy_calls > 0);
  REQUIRE(fixture.consumer.destroy_calls == fixture.consumer.create_calls);
  REQUIRE(fixture.consumer.live_buffers() == 0);
}

TEST_CASE("render: a frame with no data source produces no draw groups")
{
  // The floor case, and the one that says an empty frame is empty rather than stale: a renderer with
  // nothing registered must not emit last frame's groups.
  recording_consumer_t consumer;
  dew_renderer_t *renderer = dew_renderer_create();
  REQUIRE(renderer);
  dew_renderer_set_callback(renderer, consumer.callbacks(), &consumer);

  dew_camera_t *camera = dew_camera_create();
  dew_camera_set_perspective(camera, 45, 800, 600, 0.01, 10000);
  const double eye[3] = {1, 1, 1};
  const double center[3] = {0, 0, 0};
  const double up[3] = {0, 0, 1};
  dew_camera_look_at(camera, eye, center, up);

  auto summary = run_frame(renderer, camera, consumer);
  REQUIRE(summary.group_count == 0);
  REQUIRE(consumer.create_calls == 0);

  dew_camera_destroy(camera);
  dew_renderer_destroy(renderer);
}
