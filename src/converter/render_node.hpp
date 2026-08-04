/************************************************************************
** dewfall - point cloud management software.
** Copyright (C) 2024  Jørgen Lind
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
#pragma once

#include "buffer.hpp"
#include "frustum_tree_walker.hpp"
#include "node_data_loader.hpp"
#include "render_node_states.hpp"
#include "virtual_node.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace dew::converter
{
using namespace dew::core;

struct resident_source_t;         // full type in resident_source.hpp (shared_ptr member only needs a forward decl)
struct dyn_points_data_handler_t; // full type in point_buffer_render_helper.hpp

struct render_node_t
{
  tree_walker_data_t walker_data;

  render_node_io_state io_state = render_node_io_state::none;
  render::load_handle_t load_handle = render::invalid_load_handle;
  render::loaded_node_data_t loaded_data;
  std::atomic<bool> convert_done{false};

  render_node_gpu_state gpu_state = render_node_gpu_state::none;
  dew_draw_type_t draw_type = dew_dyn_points_1;
  // [0]=vertex, [1]=color, [2]=camera uniform, [3]=rep_level (per-point LOD, u8x1)
  dew_buffer_t gpu_buffers[4] = {};
  dew_draw_buffer_t draw_list[6] = {};
  uint32_t point_count = 0;
  std::array<double, 3> offset = {};
  glm::mat4 camera_view = {};
  size_t gpu_memory_size = 0;

  render_node_fade_state fade_state = render_node_fade_state::fade_in;
  float fade_ms = 0.0f;

  dew_buffer_t params_buffer = {};
  glm::vec4 params_data = {1.0f, 1.0f, 0.0f, 0.0f};

  double cached_distance = 0.0;

  // Runtime per-node LOD (Approach B): points are uploaded reordered coarse->fine; prefix_count[k] is the
  // draw count for render grid width k-1. Copied from loaded_data at convert time (survives the release()).
  std::array<uint32_t, 64> prefix_count = {};
  bool has_lod_order = false;

  // Virtual-subnode anchor. When this node is a spanning leaf promoted to a virtual source, `resident` keeps
  // its morton data in memory and `virtual_root` is the cached virtual octree grown from it (owning its own
  // per-node buffers). `draw_suppressed` turns off this leaf's own monolith draw once its virtual cut is live.
  // The pre-reorder morton data, salvaged from loaded_data before its release() (leaves only). Kept until the
  // leaf first qualifies for subdivision, then consumed by build_resident_source (which takes its own ref).
  std::shared_ptr<dyn_points_data_handler_t> resident_handler;
  std::shared_ptr<resident_source_t> resident;
  // R11: the resident decode runs on convert_pool. `resident_building` means a job is in flight; it fills
  // `pending_resident` and sets `resident_ready`, which the promoter then finalizes into `resident`.
  std::shared_ptr<resident_source_t> pending_resident;
  std::atomic<bool> resident_ready{false};
  bool resident_building = false;
  std::unique_ptr<virtual_node_t> virtual_root;
  bool is_virtual_source = false;
  bool draw_suppressed = false;
  // R3/R16: the virtual cut replaces this leaf's monolith only once every selected virtual node is uploaded
  // (frontier-complete). After the cut has been complete for `monolith_free_after_frames`, the monolith's own
  // GPU buffers are freed (monolith_freed) since the additive virtual cut always keeps coverage; on
  // un-promotion the node resets io_state=none to reload.
  uint32_t virtual_cut_live_frames = 0;
  bool monolith_freed = false;
  // R5 recovery: this proven-promotable leaf lost its salvage handler (CPU-budget un-promotion genuinely frees
  // the data_handler; a leaf loaded while virtual was toggled off never had one). Since the salvage lift only
  // runs on a fresh upload, the promoter must free the monolith and reload to re-acquire the handler once
  // budget headroom returns — without this flag the leaf is stranded on its full-res monolith forever.
  bool salvage_lost = false;
};

struct frame_timings_t
{
  double tree_walk_ms = 0;
  double build_render_list_ms = 0;
  double scan_classify_ms = 0;
  double schedule_io_ms = 0;
  double normalize_ms = 0;
  double gpu_upload_ms = 0;
  double fade_ms = 0;
  double emit_ms = 0;
  double total_ms = 0;
  int walker_node_count = 0;
  uint64_t walker_total_points = 0;
  int walker_trees_to_load = 0;
  int render_list_size = 0;
  int nodes_drawn = 0;
  int io_in_flight = 0;
  int nodes_loading = 0;
  int nodes_converting = 0;
  int nodes_uploaded = 0;
  int nodes_fading_in = 0;
  int nodes_fading_out = 0;
  int uploads_this_frame = 0;
};

} // namespace dew::converter
