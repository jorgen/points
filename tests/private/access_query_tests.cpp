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

// End-to-end coverage of the public query API, against a dataset this test converts itself.
//
// The invariant that matters most here is the LOD one: a full-resolution query must return the
// SOURCE point count, not a larger number. Interior LOD nodes are subsampled copies of their
// descendants, so a walk that emitted every level it passed through would return 2-3x the points
// and still look entirely plausible. That is what "query returns exactly the converted point count"
// below is guarding.

#include <doctest/doctest.h>

#include <dew/access/query.h>

#include "dataset_impl.hpp"
#include <dew/converter/converter.h>
#include <dew/core/default_attribute_names.h>

#include <cmath>
#include <cstdio>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace
{

constexpr uint32_t k_grid = 24; // 24^3 = 13824 points
constexpr uint32_t k_point_count = k_grid * k_grid * k_grid;
constexpr double k_spacing = 1.0;

struct source_points_t
{
  std::vector<int32_t> xyz;      // interleaved, LAS-style integer coordinates
  std::vector<uint16_t> intensity;
};

source_points_t make_source()
{
  source_points_t out;
  out.xyz.reserve(k_point_count * 3);
  out.intensity.reserve(k_point_count);
  for (uint32_t z = 0; z < k_grid; z++)
    for (uint32_t y = 0; y < k_grid; y++)
      for (uint32_t x = 0; x < k_grid; x++)
      {
        out.xyz.push_back(int32_t(x));
        out.xyz.push_back(int32_t(y));
        out.xyz.push_back(int32_t(z));
        out.intensity.push_back(uint16_t(x + y * 8));
      }
  return out;
}

source_points_t g_source;
uint32_t g_emitted = 0;

dew_converter_file_pre_init_info_t pre_init(const char *, size_t, dew_error_t **)
{
  dew_converter_file_pre_init_info_t info{};
  info.approximate_point_count = k_point_count;
  info.found_point_count = 1;
  info.approximate_point_size_bytes = 16;
  info.scale[0] = info.scale[1] = info.scale[2] = k_spacing;
  info.found_scale = 1;
  return info;
}

void init(const char *, size_t, dew_converter_header_t *header, dew_attributes_t *attributes, void **, dew_error_t **)
{
  header->point_count = k_point_count;
  for (int i = 0; i < 3; i++)
  {
    header->offset[i] = 0.0;
    header->scale[i] = k_spacing;
    header->min[i] = 0.0;
    header->max[i] = double(k_grid - 1) * k_spacing;
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

// Convert the synthetic grid into `path`, once per test run.
bool build_dataset(const char *path)
{
  g_source = make_source();
  // A leftover truncated file from an interrupted run makes dew_converter_create fail outright, so
  // start from nothing rather than relying on truncate semantics.
  std::remove(path);
  dew_error_t *error = nullptr;
  auto *converter = dew_converter_create(path, strlen(path), dew_open_file_semantics_truncate, &error);
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
  // Force real subdivision. The default limit (200k) would swallow the whole grid into one node,
  // which would make the full-resolution point-count assertion below trivially true -- there would be
  // no interior LOD nodes for a bad walk to double-count.
  dew_converter_set_node_point_limit(converter, 900);

  // Surface conversion failures instead of leaving a truncated file for the query to trip over.
  static std::string conversion_error;
  conversion_error.clear();
  dew_converter_runtime_callbacks_t runtime{};
  runtime.error = [](void *, const dew_error_t *e) {
    int code = 0; const char *msg = nullptr; size_t len = 0;
    dew_error_get_info(e, &code, &msg, &len);
    conversion_error.assign(msg ? msg : "", len);
  };
  dew_converter_set_runtime_callbacks(converter, runtime, nullptr);

  dew_converter_str_buffer name{"synthetic", 9};
  dew_converter_add_data_file(converter, &name, 1);
  dew_converter_wait_idle(converter);
  const bool ok = dew_converter_status(converter) == dew_conversion_status_completed && conversion_error.empty();
  if (!ok)
    fprintf(stderr, "CONVERSION FAILED status=%d error=%s\n", int(dew_converter_status(converter)), conversion_error.c_str());
  dew_converter_destroy(converter);
  return ok;
}

struct dataset_handle_t
{
  explicit dataset_handle_t(const char *path)
  {
    dew_error_t *error = nullptr;
    handle = dew_dataset_create(path, uint32_t(strlen(path)), nullptr, 0, nullptr, nullptr, &error);
    if (error)
      dew_error_destroy(error);
  }
  ~dataset_handle_t()
  {
    if (handle)
      dew_dataset_close(handle);
  }
  dew_dataset_t *handle = nullptr;
};

const char *k_path = "access_query_test.dew";

// Element width for a dew_type_t, so a test can check a buffer holds exactly point_count entries.
uint32_t size_for_format_bytes(dew_type_t type)
{
  switch (type)
  {
  case dew_type_u8:
  case dew_type_i8:
    return 1;
  case dew_type_u16:
  case dew_type_i16:
    return 2;
  case dew_type_u32:
  case dew_type_i32:
  case dew_type_r32:
    return 4;
  default:
    return 8;
  }
}

} // namespace

TEST_CASE("access: a full-resolution box query returns exactly the converted point count")
{
  REQUIRE(build_dataset(k_path));
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);
  REQUIRE(dew_dataset_state(dataset.handle) == dew_dataset_ready);

  dew_dataset_info_t info{};
  dew_dataset_get_info(dataset.handle, &info);
  MESSAGE("dataset aabb: [" << info.aabb_min[0] << "," << info.aabb_min[1] << "," << info.aabb_min[2] << "] - ["
                            << info.aabb_max[0] << "," << info.aabb_max[1] << "," << info.aabb_max[2] << "] scale " << info.scale);

  // A box covering everything, at full resolution.
  const char *attributes[] = {DEW_ATTRIBUTE_INTENSITY};
  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = -1.0;
    spec.aabb_max[i] = double(k_grid) + 1.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.attribute_names = attributes;
  spec.attribute_count = 1;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  dew_error_t *error = nullptr;
  auto *request = dew_dataset_request_region(dataset.handle, &spec, &error);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);

  dew_request_result_t result{};
  REQUIRE(dew_request_get_result(request, &result) == 1);
  MESSAGE("points: " << result.point_count << " over " << result.node_count << " nodes");

  // THE load-bearing assertion. Emitting a node and its ancestors would inflate this well past the
  // source count while every individual point still looked valid.
  REQUIRE(result.point_count == k_point_count);
  REQUIRE(result.buffer_count == 2);
  REQUIRE(result.buffers[0].size_bytes == uint64_t(k_point_count) * 3 * sizeof(double));
  REQUIRE(result.buffers[1].size_bytes == uint64_t(k_point_count) * sizeof(uint16_t));

  dew_request_release(request);
}

TEST_CASE("access: decoded positions land inside the dataset bounds and on the source grid")
{
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);
  REQUIRE(dew_dataset_state(dataset.handle) == dew_dataset_ready);

  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = -1.0;
    spec.aabb_max[i] = double(k_grid) + 1.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);

  dew_request_result_t result{};
  REQUIRE(dew_request_get_result(request, &result) == 1);
  const auto *positions = static_cast<const double *>(result.buffers[0].data);

  // Every point must sit on the integer grid the source used, within half a scale step. This is what
  // catches a wrong cell origin or a lost high-order morton bit -- both of which would still produce
  // plausible-looking coordinates.
  for (uint64_t i = 0; i < result.point_count; i++)
  {
    for (int c = 0; c < 3; c++)
    {
      const double v = positions[i * 3 + size_t(c)];
      REQUIRE(v >= -0.5);
      REQUIRE(v <= double(k_grid - 1) + 0.5);
      REQUIRE(std::fabs(v - std::round(v)) < 0.5 * k_spacing);
    }
  }
  dew_request_release(request);
}

TEST_CASE("access: point clipping returns only the points inside the box")
{
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  // A sub-box of the grid: x,y,z in [4, 12] inclusive -> 9^3 points.
  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = 4.0 - 0.25;
    spec.aabb_max[i] = 12.0 + 0.25;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_point;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);

  dew_request_result_t result{};
  REQUIRE(dew_request_get_result(request, &result) == 1);
  MESSAGE("clipped points: " << result.point_count << " (expected " << 9 * 9 * 9 << ")");
  REQUIRE(result.point_count == 9 * 9 * 9);

  const auto *positions = static_cast<const double *>(result.buffers[0].data);
  for (uint64_t i = 0; i < result.point_count; i++)
  {
    for (int c = 0; c < 3; c++)
    {
      REQUIRE(positions[i * 3 + size_t(c)] >= spec.aabb_min[c]);
      REQUIRE(positions[i * 3 + size_t(c)] <= spec.aabb_max[c]);
    }
  }
  dew_request_release(request);
}

TEST_CASE("access: node-granular clipping is a superset of point clipping")
{
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  auto query = [&](dew_clip_mode_t mode) {
    dew_region_request_t spec{};
    for (int i = 0; i < 3; i++)
    {
      spec.aabb_min[i] = 4.0;
      spec.aabb_max[i] = 12.0;
    }
    spec.lod_mode = dew_lod_full;
    spec.position_format = dew_position_r64_absolute;
    spec.clip_mode = mode;
    auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
    REQUIRE(request != nullptr);
    REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
    dew_request_result_t result{};
    REQUIRE(dew_request_get_result(request, &result) == 1);
    const uint64_t count = result.point_count;
    dew_request_release(request);
    return count;
  };

  const uint64_t node_points = query(dew_clip_node);
  const uint64_t point_points = query(dew_clip_point);
  MESSAGE("node-clipped " << node_points << " vs point-clipped " << point_points);
  REQUIRE(point_points <= node_points);
  REQUIRE(point_points == 9 * 9 * 9);
}

TEST_CASE("access: copy-out matches the borrowed buffer, and refuses a short destination")
{
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  const char *attributes[] = {DEW_ATTRIBUTE_INTENSITY};
  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = 0.0;
    spec.aabb_max[i] = 4.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.attribute_names = attributes;
  spec.attribute_count = 1;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_point;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);

  dew_request_result_t result{};
  REQUIRE(dew_request_get_result(request, &result) == 1);
  REQUIRE(result.point_count > 0);

  const uint64_t size = dew_request_attribute_size(request, 1);
  REQUIRE(size == result.buffers[1].size_bytes);

  std::vector<uint8_t> copy(size);
  REQUIRE(dew_request_copy_attribute(request, 1, copy.data(), copy.size(), nullptr) == size);
  REQUIRE(memcmp(copy.data(), result.buffers[1].data, size) == 0);

  // A destination that cannot hold the result must fail rather than truncate.
  dew_error_t *error = nullptr;
  std::vector<uint8_t> tiny(size / 2);
  REQUIRE(dew_request_copy_attribute(request, 1, tiny.data(), tiny.size(), &error) == 0);
  REQUIRE(error != nullptr);
  dew_error_destroy(error);

  dew_request_release(request);
}

TEST_CASE("access: completions are delivered through a shared pump, on the polling thread")
{
  // Delivery goes through the pump so a callback never runs on whichever internal thread finished
  // the work. With a SHARED pump, dew_dataset_poll must refuse (draining one subsystem of a shared
  // pump is the partial drain that loses wakeups) -- dew_pump_poll is the way in.
  dew_pump_t *pump = dew_pump_create();
  struct wake_state_t
  {
    std::atomic<int> wakes{0};
    std::mutex mutex;
    std::condition_variable cond;
    void fire()
    {
      {
        std::unique_lock<std::mutex> lock(mutex);
        wakes.fetch_add(1, std::memory_order_acq_rel);
      }
      cond.notify_all();
    }
    void wait_for_wake()
    {
      std::unique_lock<std::mutex> lock(mutex);
      cond.wait(lock, [this] { return wakes.load(std::memory_order_acquire) > 0; });
    }
  } wake;
  dew_pump_set_wake_callback(
    pump, [](void *ctx) { static_cast<wake_state_t *>(ctx)->fire(); }, &wake);

  dew_error_t *error = nullptr;
  auto *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, nullptr, pump, &error);
  REQUIRE(dataset != nullptr);
  REQUIRE(dew_dataset_state(dataset) == dew_dataset_ready);

  struct done_state_t
  {
    std::atomic<int> calls{0};
    dew_request_status_t status = dew_request_pending;
  } done;

  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = 0.0;
    spec.aabb_max[i] = 4.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;
  spec.done = [](dew_request_t *, dew_request_status_t status, void *ctx) {
    auto *state = static_cast<done_state_t *>(ctx);
    state->status = status;
    state->calls.fetch_add(1, std::memory_order_acq_rel);
  };
  spec.done_user_ptr = &done;

  auto *request = dew_dataset_request_region(dataset, &spec, nullptr);
  REQUIRE(request != nullptr);

  // Submit returns without doing the work: the request is genuinely pending, and the wake arrives
  // later, from the dataset's own thread.
  wake.wait_for_wake();
  REQUIRE(wake.wakes.load() >= 1);

  // The wake only SIGNALS. Nothing has been delivered, because delivery happens on the host thread
  // from a poll -- never on whichever internal thread finished the work.
  REQUIRE(done.calls.load() == 0);
  REQUIRE(dew_pump_pending_count(pump) == 1);

  // A shared pump means the per-dataset poll is off limits.
  REQUIRE(dew_dataset_poll(dataset) == 0);
  REQUIRE(done.calls.load() == 0);

  REQUIRE(dew_pump_poll(pump) == 1);
  REQUIRE(done.calls.load() == 1);
  REQUIRE(done.status == dew_request_completed);

  // Exactly once: waiting afterwards must not deliver it a second time.
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
  REQUIRE(done.calls.load() == 1);

  dew_request_release(request);
  dew_dataset_close(dataset);
  dew_pump_destroy(pump);
}

TEST_CASE("access: dew_request_wait delivers the callback when nothing polls")
{
  // The other delivery route: a caller that just waits should not have to poll as well. Exactly-once
  // still holds -- the pump drain and the wait both hold the request, and only one may call back.
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  std::atomic<int> calls{0};
  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = 0.0;
    spec.aabb_max[i] = 4.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;
  spec.done = [](dew_request_t *, dew_request_status_t, void *ctx) { static_cast<std::atomic<int> *>(ctx)->fetch_add(1, std::memory_order_acq_rel); };
  spec.done_user_ptr = &calls;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
  REQUIRE(calls.load() == 1);

  // A private pump: polling now finds the request already delivered and must not repeat it.
  dew_dataset_poll(dataset.handle);
  REQUIRE(calls.load() == 1);

  dew_request_release(request);
}

TEST_CASE("access: a submitted request is genuinely pending")
{
  // The property the whole coroutine change exists for. When submit ran the work inline this was
  // unobservable -- every request was already terminal by the time the caller held it, and every
  // dew_request_pending branch in the API was dead code.
  //
  // Submit many requests without waiting: at least one must still be running. A single request could
  // in principle finish before we look (small dataset, warm cache), so assert over a batch rather
  // than making the test a race.
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  constexpr int count = 32;
  std::vector<dew_request_t *> requests;
  int seen_pending = 0;
  for (int i = 0; i < count; i++)
  {
    dew_region_request_t spec{};
    for (int c = 0; c < 3; c++)
    {
      spec.aabb_min[c] = -1.0;
      spec.aabb_max[c] = double(k_grid) + 1.0;
    }
    spec.lod_mode = dew_lod_full;
    spec.position_format = dew_position_r64_absolute;
    spec.clip_mode = dew_clip_point;
    auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
    REQUIRE(request != nullptr);
    if (dew_request_status(request) == dew_request_pending)
      seen_pending++;
    requests.push_back(request);
  }
  MESSAGE("still pending immediately after submit: " << seen_pending << "/" << count);
  REQUIRE(seen_pending > 0);

  // And they all still finish correctly.
  for (auto *request : requests)
  {
    REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
    dew_request_result_t result{};
    REQUIRE(dew_request_get_result(request, &result) == 1);
    REQUIRE(result.point_count == k_point_count);
    dew_request_release(request);
  }
}

TEST_CASE("access: a wait that times out reports pending, not failure")
{
  // OpenVDS returns a bool here, so false means either "timed out" or "was cancelled" and the caller
  // cannot tell. Returning the status makes it unambiguous.
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  dew_region_request_t spec{};
  for (int c = 0; c < 3; c++)
  {
    spec.aabb_min[c] = -1.0;
    spec.aabb_max[c] = double(k_grid) + 1.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_point;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  // Zero timeout: either it has already finished, or we get pending back -- never a bogus failure.
  const auto immediate = dew_request_wait(request, 0);
  REQUIRE((immediate == dew_request_pending || immediate == dew_request_completed));

  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
  dew_request_release(request);
}

TEST_CASE("access: results are byte-identical however many decode threads run")
{
  // Decode happens on the pool, so several nodes are decoded at once. If a node wrote straight into
  // the shared concatenated buffers, the output order would depend on thread scheduling and this
  // would differ run to run. Staging per node and appending in walk order is what prevents it -- and
  // a reordering bug would still produce a plausible-looking point cloud, so only a byte comparison
  // catches it.
  auto run = [](uint32_t threads) {
    dew_dataset_options_t options{};
    options.decode_threads = threads;
    dew_error_t *error = nullptr;
    auto *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, &options, nullptr, &error);
    REQUIRE(dataset != nullptr);
    REQUIRE(dew_dataset_state(dataset) == dew_dataset_ready);

    const char *attributes[] = {DEW_ATTRIBUTE_INTENSITY};
    dew_region_request_t spec{};
    for (int i = 0; i < 3; i++)
    {
      spec.aabb_min[i] = -1.0;
      spec.aabb_max[i] = double(k_grid) + 1.0;
    }
    spec.lod_mode = dew_lod_full;
    spec.attribute_names = attributes;
    spec.attribute_count = 1;
    spec.position_format = dew_position_r64_absolute;
    spec.clip_mode = dew_clip_node;

    auto *request = dew_dataset_request_region(dataset, &spec, nullptr);
    REQUIRE(request != nullptr);
    REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
    dew_request_result_t result{};
    REQUIRE(dew_request_get_result(request, &result) == 1);

    std::vector<uint8_t> xyz(static_cast<const uint8_t *>(result.buffers[0].data), static_cast<const uint8_t *>(result.buffers[0].data) + result.buffers[0].size_bytes);
    std::vector<uint8_t> intensity(static_cast<const uint8_t *>(result.buffers[1].data), static_cast<const uint8_t *>(result.buffers[1].data) + result.buffers[1].size_bytes);
    const uint64_t count = result.point_count;
    dew_request_release(request);
    dew_dataset_close(dataset);
    return std::tuple{count, xyz, intensity};
  };

  const auto inline_decode = run(1);
  const auto parallel_decode = run(8);
  REQUIRE(std::get<0>(inline_decode) == k_point_count);
  REQUIRE(std::get<0>(parallel_decode) == std::get<0>(inline_decode));
  REQUIRE(std::get<1>(parallel_decode) == std::get<1>(inline_decode));
  REQUIRE(std::get<2>(parallel_decode) == std::get<2>(inline_decode));
}

TEST_CASE("access: attribute buffers stay aligned with the positions across mixed nodes")
{
  // Every buffer must hold exactly point_count elements. Resolving an attribute's stride lazily from
  // the first node that happens to carry it leaves any earlier node contributing nothing, which
  // silently shortens the attribute array and shifts it against xyz -- values that still look
  // entirely valid, just attached to the wrong points.
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  const char *attributes[] = {DEW_ATTRIBUTE_INTENSITY};
  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = -1.0;
    spec.aabb_max[i] = double(k_grid) + 1.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.attribute_names = attributes;
  spec.attribute_count = 1;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);
  REQUIRE(dew_request_wait(request, -1) == dew_request_completed);

  dew_request_result_t result{};
  REQUIRE(dew_request_get_result(request, &result) == 1);
  REQUIRE(result.node_count > 1); // pointless unless the walk really produced several nodes
  for (uint32_t i = 0; i < result.buffer_count; i++)
  {
    const auto &buffer = result.buffers[i];
    const uint64_t stride = uint64_t(size_for_format_bytes(buffer.type)) * uint32_t(buffer.components);
    REQUIRE(buffer.size_bytes == result.point_count * stride);
  }

  // The per-node segmentation must tile the whole result exactly, with no gaps or overlaps.
  uint64_t expected_first = 0;
  for (uint32_t i = 0; i < result.node_count; i++)
  {
    REQUIRE(result.nodes[i].first_point == expected_first);
    expected_first += result.nodes[i].point_count;
  }
  REQUIRE(expected_first == result.point_count);

  dew_request_release(request);
}

TEST_CASE("access: reads are genuinely overlapped, not merely coroutine-shaped")
{
  // The whole point of batching. Every other test here passes just as happily against a serial
  // engine -- they assert results, and the results are identical either way. This one measures the
  // read schedule: max_reads_in_flight controls how many blob reads are outstanding at once, so a
  // query issued with a budget of 1 must never exceed one in flight, and the same query with a
  // budget of 16 must exceed it. Wall-clock would be far too noisy to show this on a local file.
  //
  // The counter tracks cache MISSES, so each run needs its own dataset with a cold cache.
  auto peak_for_budget = [](uint32_t budget) {
    dew_dataset_options_t options{};
    options.max_reads_in_flight = budget;
    options.decode_threads = 4;
    dew_error_t *error = nullptr;
    auto *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, &options, nullptr, &error);
    REQUIRE(dataset != nullptr);
    REQUIRE(dew_dataset_state(dataset) == dew_dataset_ready);

    const char *attributes[] = {DEW_ATTRIBUTE_INTENSITY};
    dew_region_request_t spec{};
    for (int i = 0; i < 3; i++)
    {
      spec.aabb_min[i] = -1.0;
      spec.aabb_max[i] = double(k_grid) + 1.0;
    }
    spec.lod_mode = dew_lod_full;
    spec.attribute_names = attributes;
    spec.attribute_count = 1;
    spec.position_format = dew_position_r64_absolute;
    spec.clip_mode = dew_clip_node;

    auto *request = dew_dataset_request_region(dataset, &spec, nullptr);
    REQUIRE(request != nullptr);
    REQUIRE(dew_request_wait(request, -1) == dew_request_completed);
    dew_request_result_t result{};
    REQUIRE(dew_request_get_result(request, &result) == 1);
    REQUIRE(result.point_count == k_point_count);
    const int peak = static_cast<dew::access::dataset_impl_t *>(dataset)->reader->peak_reads_in_flight();
    dew_request_release(request);
    dew_dataset_close(dataset);
    return peak;
  };

  const int serial_peak = peak_for_budget(1);
  const int batched_peak = peak_for_budget(16);
  MESSAGE("peak reads in flight: budget 1 -> " << serial_peak << ", budget 16 -> " << batched_peak);

  // The budget is a target, not a cap: a node's position blob and its one attribute blob are always
  // issued together, so even a budget of 1 reaches 2. What must NOT happen is more than one node's
  // worth being outstanding.
  REQUIRE(serial_peak <= 2);
  // ...and with room for several nodes, several nodes' reads really are in flight together. This is
  // the assertion that fails against a serial engine, where the peak would stay at one node's worth
  // no matter how large the budget.
  REQUIRE(batched_peak > serial_peak);
  REQUIRE(batched_peak >= 8);
}

TEST_CASE("access: request status is idempotent and survives release-after-cancel")
{
  dataset_handle_t dataset(k_path);
  REQUIRE(dataset.handle != nullptr);

  dew_region_request_t spec{};
  for (int i = 0; i < 3; i++)
  {
    spec.aabb_min[i] = 0.0;
    spec.aabb_max[i] = 2.0;
  }
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  auto *request = dew_dataset_request_region(dataset.handle, &spec, nullptr);
  REQUIRE(request != nullptr);

  // Submit no longer runs the work, so wait for a terminal status first -- reading it straight after
  // submit would be racing the engine.
  const auto terminal = dew_request_wait(request, -1);
  REQUIRE(terminal != dew_request_pending);

  // Unlike OpenVDS, observing a terminal status does not consume the request: it stays readable, and
  // readable from more than one thread, until dew_request_release.
  for (int i = 0; i < 100; i++)
    REQUIRE(dew_request_status(request) == terminal);

  dew_request_cancel(request);
  REQUIRE(dew_request_status(request) == terminal); // cancelling a finished request changes nothing
  dew_request_release(request);
}
