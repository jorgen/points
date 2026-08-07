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
************************************************************************/

// Destroying a converter that is still working must not abort.
//
// WHAT THIS CATCHES, and why it is worth a file of its own. processor_t's ordered teardown joins the
// shared vio::thread_pool_t at step (2) but keeps the main and input event loops running until step
// (4) -- deliberately, because the pool's parked tasks complete on the storage loop and post to the
// tree loop. Anything running on those loops in that window which reaches thread_pool.enqueue hits
// vio::thread_pool_t's enqueue-after-stop path, which calls a BARE abort(): no message, no libc++abi
// banner, just SIGABRT. Three call sites sit in that window -- the pre-init coroutine launched from
// processor_t::handle_new_files, and point_reader_t's get_data / sort workers -- and originally only
// tree_handler_t had a barrier.
//
// HOW IT FAILS WITHOUT THE FIX. Not as an assertion: the process dies with SIGABRT and doctest never
// reports. That is the signature to recognise -- a test binary that exits 134 having printed nothing
// is this bug, not a hang. It is also why the checks below are loops rather than single shots: the
// window is timing-dependent, and it was a slow emulated CI runner (macOS x86_64 under Rosetta), not
// a developer machine, that first opened it wide enough to hit.
//
// The C API is the right level for this. dew_converter_destroy does NOT drain -- the CLI always
// calls wait_idle first, which is exactly why the hazard stayed hidden there, while the Python
// bindings (which destroy implicitly on GC) reproduced it as exit 134.

#include <doctest/doctest.h>

#include <dew/converter/converter.h>
#include <dew/core/default_attribute_names.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t k_points = 40000;

uint32_t g_emitted = 0;

dew_converter_file_pre_init_info_t good_pre_init(const char *, size_t, dew_error_t **)
{
  dew_converter_file_pre_init_info_t info{};
  info.approximate_point_count = k_points;
  info.approximate_point_size_bytes = 14;
  info.input_file_size_bytes = uint64_t(k_points) * 14;
  return info;
}

// The failing shape from the Python suite's error-propagation test: pre_init reports an error, so the
// file never reaches the reader and the converter goes idle down an unusual path.
dew_converter_file_pre_init_info_t failing_pre_init(const char *, size_t, dew_error_t **error)
{
  if (error)
  {
    *error = dew_error_create();
    if (*error)
    {
      const char msg[] = "pre_init refused this file";
      dew_error_set_info(*error, -1, msg, sizeof(msg) - 1);
    }
  }
  return dew_converter_file_pre_init_info_t{};
}

void init(const char *, size_t, dew_converter_header_t *header, dew_attributes_t *attributes, void **, dew_error_t **)
{
  *header = dew_converter_header_t{};
  header->point_count = k_points;
  for (int i = 0; i < 3; i++)
  {
    header->scale[i] = 0.001;
    header->offset[i] = 0.0;
    header->min[i] = 0.0;
    header->max[i] = 1000.0;
  }
  dew_attributes_add_attribute(attributes, DEW_ATTRIBUTE_XYZ, uint32_t(strlen(DEW_ATTRIBUTE_XYZ)), dew_type_i32, dew_components_3);
  g_emitted = 0;
}

void convert_data(void *, const dew_converter_header_t *, const dew_attribute_t *, uint32_t, uint32_t max_points,
                  dew_blob_t *buffers, uint32_t buffer_count, uint32_t *points_read, uint8_t *done, dew_error_t **)
{
  const uint32_t remaining = k_points - g_emitted;
  const uint32_t n = remaining < max_points ? remaining : max_points;
  if (buffer_count >= 1 && buffers[0].data)
  {
    auto *xyz = static_cast<int32_t *>(buffers[0].data);
    for (uint32_t i = 0; i < n; i++)
    {
      const uint32_t p = g_emitted + i;
      xyz[i * 3 + 0] = int32_t(p % 317);
      xyz[i * 3 + 1] = int32_t((p / 317) % 317);
      xyz[i * 3 + 2] = int32_t(p % 91);
    }
  }
  g_emitted += n;
  *points_read = n;
  *done = g_emitted >= k_points ? 1 : 0;
}

// One create/add/destroy cycle. `wait` picks whether the caller drains first (the CLI's discipline)
// or drops the converter mid-flight (what Python's GC does).
void run_cycle(const char *path, dew_converter_file_pre_init_info_t (*pre_init)(const char *, size_t, dew_error_t **), bool wait)
{
  std::remove(path);
  dew_error_t *error = nullptr;
  auto *converter = dew_converter_create(path, strlen(path), dew_open_file_semantics_truncate, &error);
  REQUIRE(converter != nullptr);

  dew_converter_file_convert_callbacks_t callbacks{};
  callbacks.pre_init = pre_init;
  callbacks.init = init;
  callbacks.convert_data = convert_data;
  dew_converter_set_file_converter_callbacks(converter, callbacks);
  dew_converter_set_node_point_limit(converter, 900); // force real subdivision, so there is tree work in flight

  dew_converter_str_buffer name{"synthetic", 9};
  dew_converter_add_data_file(converter, &name, 1);
  if (wait)
    dew_converter_wait_idle(converter);
  dew_converter_destroy(converter);
  std::remove(path);
}

} // namespace

TEST_CASE("dropping a converter while pre_init work is in flight does not abort" * doctest::timeout(300))
{
  // No wait_idle: destroy races the pre-init coroutine, which schedules onto the shared pool. Before
  // the barrier in processor_t::~processor_t this is the shortest route to the bare abort().
  for (int i = 0; i < 16; i++)
    run_cycle("converter_teardown_preinit.dew", good_pre_init, false);
  CHECK(true); // reaching here at all is the assertion; a regression kills the process instead
}

TEST_CASE("dropping a converter after a failing pre_init does not abort" * doctest::timeout(300))
{
  // The Python suite's test_python_producer_error_propagates in C form. The error path lets the
  // converter report idle while the pre-init coroutine is still retiring, so draining first is NOT
  // sufficient here -- which is why both variants are exercised.
  for (int i = 0; i < 16; i++)
  {
    run_cycle("converter_teardown_failing.dew", failing_pre_init, true);
    run_cycle("converter_teardown_failing.dew", failing_pre_init, false);
  }
  CHECK(true);
}

TEST_CASE("dropping a converter mid-conversion does not abort" * doctest::timeout(300))
{
  // Reader + sorter workers are the other two enqueue sites in the teardown window: point_reader_t's
  // handle_new_files builds a get_data worker, and handle_unsorted_points builds a sort worker, both
  // on the input loop, which outlives the pool join.
  for (int i = 0; i < 8; i++)
    run_cycle("converter_teardown_midconvert.dew", good_pre_init, false);
  CHECK(true);
}
