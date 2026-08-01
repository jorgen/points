/************************************************************************
** Points - point cloud management software.
** Copyright (C) 2022  Jørgen Lind
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
************************************************************************/
#pragma once

#include "compressor.hpp"
#include "converter.hpp"
#include "data_source_node_bbox.hpp"
#include "frustum_tree_walker.hpp"
#include "memory_budget.hpp"
#include "render_node.hpp"
#include "render_pipeline.hpp"
#include "renderer_callbacks.hpp"
#include <points/render/data_source.h>

#include <vio/thread_pool.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

struct points_converter_data_source_t
{
  points_converter_data_source_t(const std::string &url, points::render::callback_manager_t &callback_manager);
  // R14: drain in-flight convert/materialize jobs and free every GPU buffer BEFORE members tear down (the
  // convert_pool is destroyed after render_list, so without this its drain would run jobs against freed nodes).
  ~points_converter_data_source_t();

  void add_to_frame(points_frame_camera_t *camera, points_to_render_t *to_render);

  const std::string url;
  points_error_t error;
  points::converter::processor_t processor;
  points::render::callback_manager_t &callbacks;
  points_data_source_t data_source;

  std::mutex mutex;
  std::string current_attribute_name;
  std::string next_attribute_name;

  int viewport_width = 1920;
  int viewport_height = 1080;
  double screen_fraction_threshold = 0.65;
  // Runtime per-node LOD (Approach B): target on-screen size (pixels) of a rendered point's morton grid
  // cell. Each node is drawn thinned so its points sit ~this far apart on screen -> uniform density
  // independent of source density / tree depth. Larger = sparser/crisper, smaller = denser/solid.
  double render_density_px = 0.8;
  size_t gpu_memory_budget = 512 * 1024 * 1024;
  uint64_t point_budget = 10'000'000;
  // Streaming throughput. Loads are issued closest-first, so a freshly-exposed near region has to catch up
  // from coarse to fine after each camera move; too low a rate leaves it visibly sparse while a farther
  // region that was refined earlier (and is cached in the render list) still looks dense. Keep these high
  // enough that refinement converges in a second or two. Bounded overall by gpu_memory_budget.
  size_t upload_budget_per_frame = 6 * 1024 * 1024;
  int max_in_flight_io = 64;
  int max_new_io_per_frame = 16;

  // Total CPU-memory budget for the streaming renderer (the one consumer knob; GPU has its own budget above).
  // derive_budgets() splits it into the read-cache size, the decoded-backlog byte cap, the virtual-resident
  // budget, and an in-flight-IO clamp; the ctor and set_memory_budget() keep derived state + the storage
  // caches in sync. Default 1GB reproduces the historical sub-budget defaults exactly.
  uint64_t total_memory_budget = uint64_t(1024) * 1024 * 1024;
  points::converter::derived_budgets_t derived_budgets = points::converter::derive_budgets(total_memory_budget);
  // Heap-pressure brake (wasm): watches the real heap vs its link-time ceiling each frame and tightens the
  // per-frame caps at 80%/90% so the hard OOM trap is never reached. Inert on native (probe reports 0/0).
  // Tests inject a fake probe; when unset the emscripten probe (or 0/0) is used.
  points::converter::brake_level_t brake_level = points::converter::brake_level_t::none;
  std::function<void(uint64_t &heap_bytes, uint64_t &heap_max)> heap_probe_override;
  uint64_t heap_bytes_last = 0;
  uint64_t heap_max_last = 0;
  // Stats published for any-thread readers (get_memory_stats / get_virtual_stats): the render thread writes
  // them outside the mutex, so they are relaxed atomics rather than mutex-guarded fields.
  std::atomic<uint64_t> decoded_backlog_bytes_last{0};
  std::atomic<uint64_t> resident_cpu_published{0};

  points_buffer_t index_buffer;

  std::unique_ptr<points::render::node_data_loader_t> node_loader;
  vio::thread_pool_t convert_pool{std::max(2u, std::thread::hardware_concurrency() / 2)};
  points::converter::render_list_t render_list;
  // Departed nodes whose worker job (convert / resident-build / virtual materialize) is still in flight.
  // build_render_list parks them here instead of spin-waiting; add_to_frame retries them each frame. This
  // is what makes camera-move eviction non-blocking on the main thread.
  points::converter::render_list_t pending_destroy;
  // Decoded CPU buffers from nodes uploaded this frame, handed off to be freed on a convert_pool worker
  // instead of on the render thread (freeing MB-sized shared_ptr[] cascades was the top main-thread cost).
  // Render-thread-only (only add_to_frame touches it), so no lock.
  std::vector<points::render::loaded_node_data_t> cpu_reap_queue;

  uint64_t points_rendered_last_frame = 0;
  points::converter::frame_timings_t frame_timings;

  points::converter::compression_stats_t attribute_stats;
  double current_attr_min = 0.0;
  double current_attr_max = 1.0;

  std::vector<std::string> cached_walker_attribute_names;
  std::string cached_walker_attribute_source;

  float fade_duration_ms = points::converter::default_fade_duration_ms;
  std::chrono::high_resolution_clock::time_point last_frame_time;

  std::unique_ptr<points::converter::node_bbox_data_source_t> bbox_data_source;
  points::converter::node_aabb_t tight_aabb_accumulator = {{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()},
                                        {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()}};
  bool show_bounding_boxes = false;
  bool debug_transitions = false;
  points::converter::node_set_t previously_subdivided;

  // Virtual subnodes: promote spanning leaves to a render-time balanced LOD tree. A/B toggle on one camera path.
  bool enable_virtual_subtrees = true;
  uint32_t virtual_min_points = 256;         // don't subdivide tiny leaves
  uint32_t virtual_max_promotions_per_frame = 4; // ramp the one-time resident decodes so they don't hitch
  size_t virtual_gpu_used = 0;               // running total of GPU bytes held by uploaded virtual nodes
  size_t resident_cpu_used = 0;              // CPU bytes pinned by promoted leaves' resident_source_t
  size_t cpu_resident_budget = 256 * 1024 * 1024; // over this -> un-promote the farthest leaf (fall back to monolith)
  uint32_t virtual_frame_counter = 0;        // monotonic per-frame counter (virtual selection + TTL age)
  int last_virtual_promoted = -1;            // report promotions only on change (avoid per-frame console spam)
  int virtual_promoted_last = 0;             // stats: promoted spanning leaves last frame
  int virtual_nodes_drawn_last = 0;          // stats: virtual nodes drawn last frame
  bool virtual_animating = false;            // a resident build / materialize / fade is pending -> keep ticking
  std::vector<float> virtual_lod_random_offsets; // deterministic per-cell pick, identical to the converter's
};
