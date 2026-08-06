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

// The GENERATED C++ await wrappers, driven on a real vio loop.
//
// dew/await.hpp is produced from the `//= awaitable:` annotations, and test_ir.py already
// checks the checked-in copy matches the annotations. That is a text comparison, though: it proves
// the file is current, not that the code in it works. These run it.
//
// It matters most for the part a consumer cannot see: the awaiters suspend on a predicate that is
// only re-evaluated after dew_pump_poll, and the poll only happens because the wake posted to the
// loop. If any link in that chain broke, an await would simply never resume -- a hang, not a wrong
// answer, which no compile-time check can catch.

#include <doctest/doctest.h>

#include <dew/access/query_async.hpp>
#include <dew/access/query.h>
#include <dew/converter/converter.h>
#include <dew/core/default_attribute_names.h>
#include <dew/core/error.h>

#include <vio/event_loop.h>
#include <vio/task.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t k_grid = 24;
constexpr uint32_t k_point_count = k_grid * k_grid * k_grid;
constexpr double k_spacing = 0.01;

std::vector<int32_t> g_xyz;
uint32_t g_emitted = 0;

dew_converter_file_pre_init_info_t pre_init(const char *, size_t, dew_error_t **)
{
  dew_converter_file_pre_init_info_t info{};
  info.approximate_point_count = k_point_count;
  info.found_point_count = 1;
  info.approximate_point_size_bytes = 12;
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
  g_emitted = 0;
}

void convert_data(void *, const dew_converter_header_t *, const dew_attribute_t *, uint32_t, uint32_t max_points, dew_blob_t *buffers, uint32_t buffer_count, uint32_t *points_read, uint8_t *done,
                  dew_error_t **)
{
  const uint32_t remaining = k_point_count - g_emitted;
  const uint32_t n = remaining < max_points ? remaining : max_points;
  if (buffer_count >= 1)
    memcpy(buffers[0].data, g_xyz.data() + size_t(g_emitted) * 3, size_t(n) * 3 * sizeof(int32_t));
  g_emitted += n;
  *points_read = n;
  *done = g_emitted >= k_point_count ? 1 : 0;
}

bool build_dataset(const char *path)
{
  g_xyz.clear();
  for (uint32_t z = 0; z < k_grid; z++)
    for (uint32_t y = 0; y < k_grid; y++)
      for (uint32_t x = 0; x < k_grid; x++)
      {
        g_xyz.push_back(int32_t(x));
        g_xyz.push_back(int32_t(y));
        g_xyz.push_back(int32_t(z));
      }
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
  dew_converter_set_node_point_limit(converter, 512); // force subdivision
  dew_converter_str_buffer name{"synthetic", 9};
  dew_converter_add_data_file(converter, &name, 1);
  dew_converter_wait_idle(converter);
  const bool ok = dew_converter_status(converter) == dew_conversion_status_completed;
  dew_converter_destroy(converter);
  return ok;
}

const char *k_path = "cpp_await_test.dew";

struct outcome_t
{
  int dataset_state = -1;
  int request_status = -1;
  uint64_t point_count = 0;
  bool observed_pending = false;
  bool observed_opening = false;
};

// The shape a consumer writes: open, await ready, submit, await completion -- with no blocking call
// anywhere and the loop free the whole time.
vio::task_t<void> run(vio::event_loop_t &loop, outcome_t &out)
{
  dewpp::async::driver_t driver(loop);

  dew_error_t *error = nullptr;
  dew_dataset_t *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, nullptr, driver.pump(), &error);
  REQUIRE(dataset);
  if (error)
    dew_error_destroy(error);

  // create() returns before the index read, so this is `opening` -- the property the whole design
  // rests on, and the reason there is anything to await at all.
  out.observed_opening = dew_dataset_state(dataset) == dew_dataset_opening;

  out.dataset_state = int(co_await dewpp::async::ready(driver, dataset));
  if (out.dataset_state != int(dew_dataset_ready))
  {
    dew_dataset_close(dataset);
    loop.stop();
    co_return;
  }

  dew_dataset_info_t info = {};
  dew_dataset_get_info(dataset, &info);

  dew_region_request_t spec = {};
  memcpy(spec.aabb_min, info.aabb_min, sizeof(spec.aabb_min));
  memcpy(spec.aabb_max, info.aabb_max, sizeof(spec.aabb_max));
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  dew_request_t *raw = dew_dataset_request_region(dataset, &spec, &error);
  REQUIRE(raw);
  // The sync wrapper owns the handle (//= destroy: dew_request_release), so an early return
  // cannot leak it -- and ready() takes the wrapper directly.
  dewpp::request_t request(raw);

  out.observed_pending = request.status() == dew_request_pending;
  out.request_status = int(co_await dewpp::async::ready(driver, request));

  // Through the sync wrapper's own methods: a gated bool + struct out-param becomes an optional.
  if (auto result = request.get_result())
    out.point_count = result->point_count;

  request.reset(); // invalidates `result`
  dew_dataset_close(dataset);
  loop.stop();
  co_return;
}

// NOTE for anyone adding a test here: the coroutine body must be a NAMED function that the
// run_in_loop lambda merely forwards to, never a coroutine lambda. A lambda's captures live in the
// closure temporary, which is destroyed after the first suspension -- so a coroutine lambda reads
// freed memory the moment it resumes. (It segfaults, which is how this note came to be written.)
// loop_blocking.hpp documents the same rule for the same reason.
vio::task_t<void> run_double_await(vio::event_loop_t &loop, bool &returned, int &second_status)
{
  dewpp::async::driver_t driver(loop);
  dew_error_t *error = nullptr;
  dew_dataset_t *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, nullptr, driver.pump(), &error);
  REQUIRE(dataset);
  if (error)
    dew_error_destroy(error);
  REQUIRE(co_await dewpp::async::ready(driver, dataset) == dew_dataset_ready);
  // Second time: already terminal, so this must fall straight through rather than park forever
  // waiting for a wake that is never coming.
  second_status = int(co_await dewpp::async::ready(driver, dataset));
  returned = true;
  dew_dataset_close(dataset);
  loop.stop();
  co_return;
}

vio::task_t<void> run_guard(vio::event_loop_t &loop)
{
  dewpp::async::driver_t driver(loop);
  dew_error_t *error = nullptr;
  dew_dataset_t *dataset = dew_dataset_create(k_path, uint32_t(strlen(k_path)), nullptr, 0, nullptr, driver.pump(), &error);
  REQUIRE(dataset);
  if (error)
    dew_error_destroy(error);
  REQUIRE(co_await dewpp::async::ready(driver, dataset) == dew_dataset_ready);

  dew_dataset_info_t info = {};
  dew_dataset_get_info(dataset, &info);
  dew_region_request_t spec = {};
  memcpy(spec.aabb_min, info.aabb_min, sizeof(spec.aabb_min));
  memcpy(spec.aabb_max, info.aabb_max, sizeof(spec.aabb_max));
  spec.lod_mode = dew_lod_full;
  spec.position_format = dew_position_r64_absolute;
  spec.clip_mode = dew_clip_node;

  {
    dewpp::request_t request(dew_dataset_request_region(dataset, &spec, &error));
    REQUIRE(request.handle());
    co_await dewpp::async::ready(driver, request);
    request.reset();
    REQUIRE(request.handle() == nullptr);
    // Destructor runs here on an already-reset wrapper.
  }
  dew_dataset_close(dataset);
  loop.stop();
  co_return;
}

} // namespace

TEST_CASE("cpp await: a dataset and a request are awaited on the host's own vio loop")
{
  REQUIRE(build_dataset(k_path));

  outcome_t out;
  vio::event_loop_t loop;
  loop.run_in_loop([&loop, &out]() { return run(loop, out); });
  loop.run(); // returns only when run() calls stop -- so reaching here at all means both awaits resumed

  // Both awaits genuinely suspended: the handles were non-terminal when they were created, so
  // resuming required the wake -> loop -> poll chain to work end to end.
  REQUIRE(out.observed_opening);
  REQUIRE(out.observed_pending);

  REQUIRE(out.dataset_state == int(dew_dataset_ready));
  REQUIRE(out.request_status == int(dew_request_completed));
  REQUIRE(out.point_count == k_point_count);
}

TEST_CASE("cpp await: awaiting an already-terminal handle does not suspend")
{
  // await_ready short-circuits, which is what keeps a second await cheap -- and, more importantly,
  // is what stops a caller hanging when it awaits a handle that finished before it looked.
  REQUIRE(build_dataset(k_path));

  bool returned = false;
  int second_status = -1;
  vio::event_loop_t loop;
  loop.run_in_loop([&loop, &returned, &second_status]() { return run_double_await(loop, returned, second_status); });
  loop.run();

  REQUIRE(returned);
  REQUIRE(second_status == int(dew_dataset_ready));
}

TEST_CASE("cpp await: the request wrapper releases exactly once")
{
  // dew_request_release is idempotent, but the wrapper still has to null its handle -- otherwise an
  // explicit reset() followed by the destructor would release a request the dataset has already
  // dropped, which is a use-after-free rather than a no-op. This is the //= destroy: annotation
  // doing its job: without it dewpp::request_t would be a borrowed view and leak every request.
  REQUIRE(build_dataset(k_path));

  vio::event_loop_t loop;
  loop.run_in_loop([&loop]() { return run_guard(loop); });
  loop.run();
}
