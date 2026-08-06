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

// The GENERATED C++ wrapper, doing real work.
//
// test_ir.py already checks bindings/cpp/dew/dewpp.hpp matches the annotations and that nothing was
// silently skipped. Both are text comparisons: they prove the file is current, not that the code in
// it is right. A wrapper can be perfectly up to date and still pass an argument in the wrong slot,
// leak a handle, or report success on a failure.
//
// So this converts a dataset and queries it back THROUGH the wrapper, and checks the pieces the
// generator actually had to get right: RAII ownership, static factories returning std::expected, the
// two-call out_string convention, error accessors that are accessors rather than failures, and
// (pointer, length) string pairs.

#include <doctest/doctest.h>

#include <dew/dewpp.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t k_grid = 16;
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

const char *k_path = "dewpp_test.dew";

// Convert through the WRAPPER, not the C API: this is the half that exercises the factory, the
// callback-struct passthrough, and the (pointer, length) string pairs.
bool build_dataset()
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
  std::remove(k_path);

  auto opened = dewpp::converter::create(k_path, dew_open_file_semantics_truncate);
  if (!opened)
    return false;
  dewpp::converter converter = std::move(*opened);

  dew_converter_file_convert_callbacks_t callbacks{};
  callbacks.pre_init = pre_init;
  callbacks.init = init;
  callbacks.convert_data = convert_data;
  converter.set_file_converter_callbacks(callbacks);
  converter.set_node_point_limit(512);

  std::vector<dew_converter_str_buffer> names{dew_converter_str_buffer{"synthetic", 9}};
  converter.add_data_file(names);
  converter.wait_idle();
  return converter.status() == dew_conversion_status_completed;
}

} // namespace

TEST_CASE("dewpp: a dataset converts and queries back through the generated wrapper")
{
  REQUIRE(build_dataset());

  auto opened = dewpp::dataset::create(k_path, "", dew_dataset_options_t{}, dewpp::pump());
  REQUIRE(opened);
  dewpp::dataset dataset = std::move(*opened);

  // Opening is deferred, so the wrapper has to expose the wait as well as the state.
  REQUIRE(dataset.wait_ready(-1) == dew_dataset_ready);
  REQUIRE(dataset.state() == dew_dataset_ready);

  // The two-call out_string convention, reduced to returning a std::string.
  REQUIRE(dataset.attribute_count() >= 1);
  const std::string name = dataset.get_attribute_name(0);
  REQUIRE(name == "xyz");

  // A method whose only result is a struct out-param returns the struct itself.
  dew_dataset_info_t info = dataset.get_info();
  REQUIRE(info.scale > 0.0);

  // ...and an error ACCESSOR is an accessor: nothing went wrong, so there is nothing to report.
  REQUIRE(!dataset.get_error().has_value());
}

TEST_CASE("dewpp: handles are move-only and destroy exactly once")
{
  // The generated RAII is the part with the most room to be subtly wrong: a copy would double-free,
  // and a move that forgot to null the source would too.
  REQUIRE(build_dataset());

  static_assert(!std::is_copy_constructible_v<dewpp::converter>, "an owning handle must not be copyable");
  static_assert(std::is_move_constructible_v<dewpp::converter>, "an owning handle must be movable");
  // A borrowed view is the opposite: the library owns it, so copying is free and correct.
  static_assert(std::is_copy_constructible_v<dewpp::attributes>, "a borrowed view should be copyable");

  auto opened = dewpp::converter::create("dewpp_move_test.dew", dew_open_file_semantics_truncate);
  REQUIRE(opened);
  dewpp::converter first = std::move(*opened);
  REQUIRE(first);
  dew_converter_t *raw = first.handle();

  dewpp::converter second = std::move(first);
  REQUIRE(second.handle() == raw);
  REQUIRE(!first); // moved-from is empty, so its destructor does nothing

  // release() hands ownership back out; the wrapper must then destroy nothing.
  dew_converter_t *escaped = second.release();
  REQUIRE(escaped == raw);
  REQUIRE(!second);
  dew_converter_destroy(escaped);
  std::remove("dewpp_move_test.dew");
}

TEST_CASE("dewpp: a failing factory returns the error rather than a handle")
{
  // The whole reason handles are created through a static factory: with -fno-exceptions a
  // constructor cannot report. An unwritable path is the cheapest genuine failure.
  auto opened = dewpp::converter::create("/definitely/not/a/directory/out.dew", dew_open_file_semantics_truncate);
  REQUIRE(!opened.has_value());
  // And the message survives the copy out of the dew_error_t, which is why dewpp::error is a value.
  REQUIRE(!opened.error().message().empty());
}
