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
#include <doctest/doctest.h>

#include "data_source.hpp" // render::frame_camera_cpp_t
#include "memory_budget.hpp"
#include "render_pipeline.hpp"
#include "renderer_callbacks.hpp"

#include <vio/thread_pool.h>

namespace
{
using namespace dew;
using namespace dew::converter;
using namespace dew::core;

constexpr uint64_t operator""_mb(unsigned long long v)
{
  return uint64_t(v) * 1024 * 1024;
}

TEST_CASE("derive_budgets(1GB) reproduces the historical sub-budget defaults exactly")
{
  const auto d = derive_budgets(1024_mb);
  REQUIRE(d.total == 1024_mb);
  REQUIRE(d.read_cache_bytes == 256_mb);
  REQUIRE(d.decompressed_cache_bytes == 64_mb);
  REQUIRE(d.decoded_backlog_cap == 256_mb);
  REQUIRE(d.cpu_resident_budget == 256_mb);
  REQUIRE(d.io_clamp == 64);
}

TEST_CASE("derive_budgets mobile target and clamps")
{
  const auto mobile = derive_budgets(256_mb);
  REQUIRE(mobile.read_cache_bytes == 64_mb);
  REQUIRE(mobile.decompressed_cache_bytes == 16_mb);
  REQUIRE(mobile.decoded_backlog_cap == 64_mb);
  REQUIRE(mobile.cpu_resident_budget == 64_mb);
  REQUIRE(mobile.io_clamp == 16);

  // Floor: even a degenerate budget keeps workable minimums.
  const auto tiny = derive_budgets(0);
  REQUIRE(tiny.read_cache_bytes == 16_mb);
  REQUIRE(tiny.decompressed_cache_bytes == 8_mb);
  REQUIRE(tiny.decoded_backlog_cap == 24_mb);
  REQUIRE(tiny.cpu_resident_budget == 32_mb);
  REQUIRE(tiny.io_clamp == 6); // 24MB / 4MB

  // Ceiling: a huge budget never exceeds the historical caps.
  const auto huge = derive_budgets(64ull * 1024_mb);
  REQUIRE(huge.read_cache_bytes == 256_mb);
  REQUIRE(huge.decompressed_cache_bytes == 64_mb);
  REQUIRE(huge.decoded_backlog_cap == 256_mb);
  REQUIRE(huge.cpu_resident_budget == 256_mb);
  REQUIRE(huge.io_clamp == 64);

  // Monotone in the total.
  uint64_t prev_backlog = 0;
  for (uint64_t b = 64_mb; b <= 2048_mb; b += 64_mb)
  {
    const auto d = derive_budgets(b);
    REQUIRE(d.decoded_backlog_cap >= prev_backlog);
    prev_backlog = d.decoded_backlog_cap;
  }
}

TEST_CASE("compute_brake_level boundaries")
{
  REQUIRE(compute_brake_level(0, 0) == brake_level_t::none);           // native: no probe
  REQUIRE(compute_brake_level(2048_mb, 0) == brake_level_t::none);     // no ceiling -> no brake
  // Scale-free thresholds; 1000 divides evenly so the boundaries are exact.
  const uint64_t max = 1000;
  REQUIRE(compute_brake_level(0, max) == brake_level_t::none);
  REQUIRE(compute_brake_level(799, max) == brake_level_t::none);
  REQUIRE(compute_brake_level(800, max) == brake_level_t::high);
  REQUIRE(compute_brake_level(899, max) == brake_level_t::high);
  REQUIRE(compute_brake_level(900, max) == brake_level_t::critical);
  REQUIRE(compute_brake_level(max, max) == brake_level_t::critical);
  // And at a realistic scale: 1GB ceiling, 820MB heap -> high.
  REQUIRE(compute_brake_level(820_mb, 1024_mb) == brake_level_t::high);
}

static tree_walker_data_t make_walker_data(uint32_t point_count, bool with_attribute)
{
  tree_walker_data_t w; // no brace-init: point_count_t's explicit default ctor rejects aggregate {}
  w.aabb = {};
  w.tight_aabb = {};
  w.point_count.data = point_count;
  w.format[0] = point_format_t(dew_type_m64, dew_components_1);
  w.locations[0].size = point_count * 8;
  if (with_attribute)
  {
    w.format[1] = point_format_t(dew_type_u16, dew_components_3);
    w.locations[1].size = point_count * 6;
  }
  return w;
}

TEST_CASE("node byte estimators match the decode/upload layout")
{
  const uint32_t pc = 100'000;
  const auto w = make_walker_data(pc, true);
  // Inputs: m64 codes (8) + u16x3 attribute (6).
  REQUIRE(estimate_node_input_bytes(w) == uint64_t(pc) * (8 + 6));
  // + outputs: r32x3 vertex (12) + attribute copy (6) + rep_level (1).
  REQUIRE(estimate_node_cpu_bytes(w) == uint64_t(pc) * (8 + 6) + uint64_t(pc) * (12 + 6 + 1));
  // GPU: vertex (12) + attribute as uploaded (u16x3/rgb is exempt from float normalization -> raw 6) +
  // rep_level (1) + camera mat4 + params vec4.
  REQUIRE(estimate_node_gpu_bytes(w) == uint64_t(pc) * (12 + 6 + 1) + 64 + 16);
  // A normalizing attribute (u32x1) uploads as float-per-component: max(raw 4, normalized 4) = 4.
  auto wn = make_walker_data(pc, false);
  wn.format[1] = point_format_t(dew_type_u8, dew_components_1);
  wn.locations[1].size = pc;
  REQUIRE(estimate_node_gpu_bytes(wn) == uint64_t(pc) * (12 + 4 + 1) + 64 + 16);

  const auto bare = make_walker_data(pc, false);
  REQUIRE(estimate_node_input_bytes(bare) == uint64_t(pc) * 8);
  REQUIRE(estimate_node_cpu_bytes(bare) == uint64_t(pc) * 8 + uint64_t(pc) * (12 + 1));
  REQUIRE(estimate_node_gpu_bytes(bare) == uint64_t(pc) * (12 + 1) + 64 + 16);
}

// A loader that accepts every request but never completes it: nodes stay in `loading`, so the byte
// accounting sees exactly the scheduled set.
struct stub_node_loader_t : render::node_data_loader_t
{
  uint64_t next_handle = 1;
  int requests = 0;
  render::load_handle_t request_load(const void *, uint32_t) override
  {
    requests++;
    return next_handle++;
  }
  bool is_ready(render::load_handle_t) override
  {
    return false;
  }
  render::loaded_node_data_t get_data(render::load_handle_t) override
  {
    return {};
  }
  void cancel(render::load_handle_t) override
  {
  }
};

static render_list_t make_render_list(int count, uint32_t points_each)
{
  render_list_t list;
  for (int i = 0; i < count; i++)
  {
    auto node = std::make_unique<render_node_t>();
    node->walker_data = make_walker_data(points_each, true);
    // Spread the nodes out so the closest-first sort has a deterministic order.
    node->walker_data.tight_aabb.min = {double(i), 0.0, 0.0};
    node->walker_data.tight_aabb.max = {double(i) + 0.5, 0.5, 0.5};
    list.push_back(std::move(node));
  }
  return list;
}

TEST_CASE("decoded-backlog byte cap gates IO scheduling")
{
  render::callback_manager_t callbacks(nullptr); // zeroed callbacks: every do_* is a no-op
  stub_node_loader_t loader;
  vio::thread_pool_t pool(1);
  const uint32_t pc = 100'000;
  auto render_list = make_render_list(10, pc);
  const uint64_t per_node = estimate_node_cpu_bytes(render_list[0]->walker_data);

  io_limits_t limits;
  limits.max_concurrent_io = 64;
  limits.max_new_io_per_frame = 64;
  limits.decoded_backlog_cap = per_node * 3; // room for exactly 3 nodes
  limits.gpu_memory_budget = 512_mb;

  render::frame_camera_cpp_t camera = {};
  auto stats = process_io_and_upload(render_list, glm::dvec3(0.0), tree_config_t(), callbacks, &loader, pool,
                                     camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats.io_scheduled == 3);
  REQUIRE(stats.io_denied_backlog > 0);
  REQUIRE(stats.io_denied_gpu == 0);
  REQUIRE(loader.requests == 3);
  REQUIRE(stats.backlog_bytes == per_node * 3);

  // Next frame: the 3 in-flight loads keep their charge, no further IO fits.
  auto stats2 = process_io_and_upload(render_list, glm::dvec3(0.0), tree_config_t(), callbacks, &loader, pool,
                                      camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats2.io_scheduled == 0);
  REQUIRE(stats2.io_in_flight == 3);
  REQUIRE(stats2.io_denied_backlog > 0);
  REQUIRE(loader.requests == 3);
}

TEST_CASE("GPU-fit pre-check refuses IO for nodes the GPU budget cannot accept")
{
  render::callback_manager_t callbacks(nullptr);
  stub_node_loader_t loader;
  vio::thread_pool_t pool(1);
  auto render_list = make_render_list(4, 100'000);
  const uint64_t per_node_gpu = estimate_node_gpu_bytes(render_list[0]->walker_data);

  io_limits_t limits;
  limits.max_concurrent_io = 64;
  limits.max_new_io_per_frame = 64;
  limits.decoded_backlog_cap = 256_mb;
  limits.gpu_memory_budget = per_node_gpu; // exactly one node fits

  render::frame_camera_cpp_t camera = {};
  auto stats = process_io_and_upload(render_list, glm::dvec3(0.0), tree_config_t(), callbacks, &loader, pool,
                                     camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats.io_scheduled == 1);
  REQUIRE(stats.io_denied_gpu > 0);
  REQUIRE(loader.requests == 1);

  // Zero GPU budget -> nothing is even loaded.
  stub_node_loader_t loader2;
  auto render_list2 = make_render_list(4, 100'000);
  limits.gpu_memory_budget = 0;
  auto stats2 = process_io_and_upload(render_list2, glm::dvec3(0.0), tree_config_t(), callbacks, &loader2, pool,
                                      camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats2.io_scheduled == 0);
  REQUIRE(stats2.io_denied_gpu > 0);
  REQUIRE(loader2.requests == 0);
}

TEST_CASE("a single node larger than the backlog cap still makes progress when the pipeline is empty")
{
  render::callback_manager_t callbacks(nullptr);
  stub_node_loader_t loader;
  vio::thread_pool_t pool(1);
  auto render_list = make_render_list(2, 1'000'000);

  io_limits_t limits;
  limits.max_concurrent_io = 64;
  limits.max_new_io_per_frame = 64;
  limits.decoded_backlog_cap = 1024 * 1024; // far below one node's estimate
  limits.gpu_memory_budget = 512_mb;

  render::frame_camera_cpp_t camera = {};
  auto stats = process_io_and_upload(render_list, glm::dvec3(0.0), tree_config_t(), callbacks, &loader, pool,
                                     camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats.io_scheduled == 1); // closest admitted despite exceeding the cap -- must make progress
  REQUIRE(stats.io_denied_backlog == 1);
  REQUIRE(loader.requests == 1);
}

TEST_CASE("deferred (pending-destroy) bytes pre-charge the backlog cap")
{
  render::callback_manager_t callbacks(nullptr);
  stub_node_loader_t loader;
  vio::thread_pool_t pool(1);
  auto render_list = make_render_list(4, 100'000);
  const uint64_t per_node = estimate_node_cpu_bytes(render_list[0]->walker_data);

  io_limits_t limits;
  limits.max_concurrent_io = 64;
  limits.max_new_io_per_frame = 64;
  limits.decoded_backlog_cap = per_node * 3;
  limits.gpu_memory_budget = 512_mb;
  limits.deferred_backlog_bytes = per_node * 2; // two departed nodes still pin their decode buffers

  render::frame_camera_cpp_t camera = {};
  auto stats = process_io_and_upload(render_list, glm::dvec3(0.0), tree_config_t(), callbacks, &loader, pool,
                                     camera, limits, 0.0, 1.0, false, 0, nullptr);
  REQUIRE(stats.io_scheduled == 1);
  REQUIRE(stats.io_denied_backlog > 0);
}

} // namespace
