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

#include "frustum_tree_walker.hpp" // node_aabb_t + glm
#include "morton.hpp"
#include "node_data_loader.hpp" // dew_buffer_t, dew_draw_type_t
#include "render_node_states.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace dew::converter
{

enum class virtual_mat_state : uint8_t
{
  none,          // no owned buffer yet
  materializing, // a convert_pool worker is decoding/quantizing this node's slice
  materialized,  // vertex_data/attribute_data ready to upload (convert_done set)
};

// A node in the render-time virtual octree grown from a resident leaf's morton-sorted data. It owns nothing
// of the source (only an index window into it), but once selected it owns its OWN decoded (leaf) or quantized
// (interior) drawable buffer + GPU buffers, and runs the same upload/fade/evict lifecycle as a real node.
struct virtual_node_t
{
  uint32_t first_index = 0; // window [first_index, first_index+src_count) into the resident morton codes /
  uint32_t src_count = 0;   //   decoded_vertex of the source leaf
  int level = 0;            // octree level of this virtual node (leaf_lod, leaf_lod-1, ... deeper as we split)
  morton::morton192_t octant_min = {}; // this octant's node_min (recursion origin + quantize node_min)
  node_aabb_t loose_aabb = {};  // geometric octant cube -> frustum + should_subdivide
  node_aabb_t tight_aabb = {};  // actual points' bounds -> cached_distance (honest per-subnode distance)

  uint32_t last_selected_frame = 0;   // the frame counter when the walk last visited this node ("selected" ==
                                      //   last_selected_frame == frame_index; deferred-evicted after a TTL)
  bool subdivided_last_frame = false; // fed back into should_subdivide as hysteresis (mirrors the real walker)

  // Owned drawable product (produced on convert_pool, dropped after upload). Same coarse->fine LOD ordering +
  // rep_level as a stored node, so a virtual node draws through the identical per-point-LOD + crossfade path.
  std::shared_ptr<uint8_t[]> vertex_data;    // reordered r32x3, coarse->fine
  std::shared_ptr<uint8_t[]> attribute_data; // reordered, matching perm
  std::shared_ptr<uint8_t[]> rep_level_data; // u8, reordered representative level (per-point density cull)
  std::array<uint32_t, 64> prefix_count = {}; // prefix_count[W+1] = draw count for render grid width W
  uint32_t draw_count = 0;
  std::atomic<bool> convert_done{false};

  virtual_mat_state mat_state = virtual_mat_state::none;
  render_node_gpu_state gpu_state = render_node_gpu_state::none;
  render_node_fade_state fade_state = render_node_fade_state::fade_in;
  float fade_ms = 0.0f;

  dew_draw_type_t draw_type = dew_dyn_points_1;
  dew_buffer_t gpu_buffers[4] = {}; // [0]=vertex [1]=color [2]=camera uniform [3]=rep_level
  dew_draw_buffer_t draw_list[6] = {};
  dew_buffer_t params_buffer = {};
  glm::vec4 params_data = {1.0f, 1.0f, 0.0f, 0.0f};
  glm::mat4 camera_view = {};
  size_t gpu_memory_size = 0;
  double cached_distance = 0.0;

  // Lazy-deepened child octants (built on first descent, cached across frames). The tree IS the cache.
  std::array<std::unique_ptr<virtual_node_t>, 8> children = {};
  bool children_built = false;
};

} // namespace dew::converter
