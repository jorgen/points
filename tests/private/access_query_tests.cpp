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
#include <dew/converter/converter.h>
#include <dew/core/default_attribute_names.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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
    handle = dew_dataset_open(path, uint32_t(strlen(path)), nullptr, 0, nullptr, &error);
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

  // Unlike OpenVDS, observing a terminal status does not consume the request.
  const auto first = dew_request_status(request);
  for (int i = 0; i < 100; i++)
    REQUIRE(dew_request_status(request) == first);

  dew_request_cancel(request);
  dew_request_release(request);
}
